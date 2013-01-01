#include "ext.h"
#include "z_dsp.h"
#include "ext_critical.h"
#include "buffer.h"
#include "ext_common.h"
#include "clock.h"
#include <list>
#include <string>

void		*biddler_class;			// this object
t_symbol	*ps_buffer;			// a buffer 

typedef struct _biddler				// biddler data structure
{	
	t_pxobject	l_obj;			// object info struct
	t_symbol	*l_sym;			// symbol struct
	t_symbol    *filter;
	t_buffer	*l_buf;			// buffer struct
	//unsigned long		channel;		// which channel?
	unsigned long		index;			// buffer index
	//unsigned long		frames;			// number of frames in buffer
	//unsigned long		num_chans;		// number of channels in buffer
	unsigned long		saveinuse;		// save state of buffer

	unsigned long slice_n;
	unsigned long quant_n;
	unsigned long measure_index;
	long measure_index_offset;
	unsigned long read;
	bool retrigger;
	bool follow;
	bool go;
	struct clock q_clock;
	struct clock measure_clock;
	struct clock beat_clock;
	
	std::list<t_symbol *> *b_list;
	std::list<t_symbol *>::iterator *current;
   
} t_biddler;

enum inlets {
	SIG_INDEX = 0,
	RETRIGGER,
	SLICE_N,
	QUANT_N,
	JUMP,
	TEMPO,
	TIMEBASE,
	INLET_AMT
};

#define NUM_SIG_INLETS 2

enum outlets {
	OUT_L = 0,
	OUT_R,
	POS_OUT,
	TOTAL_POS_OUT,
	BEAT_CLOCK_OUT,
	SONG_POS_OUT
};

#define NUM_SIG_OUTLETS 3

void *biddler_new(t_symbol *s, long chan );		// makes a new biddler~
void biddler_free( t_biddler *x );			// deletes biddler~
void biddler_dsp (t_biddler *x, t_signal **sp );	// sets dsp for biddler~
t_int *biddler_perform( t_int *w );			// biddler dsp perform
void biddler_dblclick( t_biddler *x );			// double-click handler
void biddler_assist( t_biddler *x, void *b, long m, long a, char *s );
void biddler_add( t_biddler *x, t_symbol *s );		// 'set' message handler

void set_slice_n(t_biddler *x, long val);
void set_quant_n(t_biddler *x, long val);
void set_jump(t_biddler *x, long val);
void set_tempo_float(t_biddler *x, float val);
void set_timebase(t_biddler *x, long val);
void bang(t_biddler *x);
void increment_buffer(t_biddler *x);
void biddler_follow(t_biddler *x, long n );
void biddler_go(t_biddler *x, long n );
void biddler_reset_position(t_biddler *x);

int main(void) {
	setup( (t_messlist **)&biddler_class, (method)biddler_new, (method)biddler_free, 
		(short)sizeof(t_biddler), 0L, 0 );
	addmess( (method)biddler_dsp, "dsp", A_CANT, 0 );		// add dsp method
	addmess( (method)biddler_add, "add", A_SYM, 0 );		// add 'set' method
	addmess( (method)biddler_follow, "follow", A_LONG, 0 );		
	addmess( (method)biddler_go, "go", A_LONG, 0 );		// 
	addmess( (method)biddler_reset_position, "reset_position", A_NOTHING, 0 );
	
	addinx((method)set_timebase, TIMEBASE);
	addftx((method)set_tempo_float, TEMPO);
	addinx((method)set_jump, JUMP);
	addinx((method)set_quant_n, QUANT_N);
	addinx((method)set_slice_n, SLICE_N);
	
	addbang((method)bang);
	addmess( (method)biddler_assist, "assist", A_CANT, 0 );	// add assist method
	addmess( (method)biddler_dblclick, "dblclick", A_CANT, 0 );	// add double-click method
	dsp_initclass();						// init the class
	ps_buffer = gensym( "buffer~" );				// store a 'buffer~' symbol
	post("biddler~ : performance tool for glitch addicts. written by anthony cantor in 2007. inspired by cooper baker's cuisanart");
	return 0;
}

void biddler_dsp(t_biddler *x, t_signal **sp) {

#define DSP_ADD_2_ARG 10 //number of passed vars to biddler perform
	dsp_add( 
		biddler_perform, 
		DSP_ADD_2_ARG, 
		sp[SIG_INDEX]->s_vec, 
		sp[RETRIGGER]->s_vec, 
		sp[NUM_SIG_INLETS+OUT_L]->s_vec, 
		sp[NUM_SIG_INLETS+OUT_R]->s_vec,
		sp[NUM_SIG_INLETS+POS_OUT]->s_vec, 
		sp[NUM_SIG_INLETS+TOTAL_POS_OUT]->s_vec,
		sp[NUM_SIG_INLETS+BEAT_CLOCK_OUT]->s_vec, 
		sp[NUM_SIG_INLETS+SONG_POS_OUT]->s_vec,
		sp[0]->s_n, 
		x 
	);

}						   

t_int *biddler_perform( t_int *w ) {

	// inlet signal vector locations
	t_float *in			= (t_float *)(w[1 + SIG_INDEX]);
	// outlet signal vector locations
	t_float *out0		= (t_float *)(w[1 + NUM_SIG_INLETS+OUT_L]);		
	t_float *out1 		= (t_float *)(w[1 + NUM_SIG_INLETS+OUT_R]);		
	t_float *pos_out 	= (t_float *)(w[1 + NUM_SIG_INLETS+POS_OUT]);		
	t_float *total_pos_out 	= (t_float *)(w[1 + NUM_SIG_INLETS+TOTAL_POS_OUT]);		
	t_float *beat_clock_out = (t_float *)(w[1 + NUM_SIG_INLETS+BEAT_CLOCK_OUT]);	
	t_float *song_pos_out 	= (t_float *)(w[1 + NUM_SIG_INLETS+SONG_POS_OUT]);		
	
	// always return w plus one more than the 2nd argument
	t_int *ret_val = w + DSP_ADD_2_ARG + 1;  
	
	// signal vector size in samples
	int n			= (int)(w[DSP_ADD_2_ARG-1]);			
	// biddler struct location
	t_biddler *x	= (t_biddler *)(w[DSP_ADD_2_ARG]);		
	float *buffer;
    float x_read_coeff;
	clock_flag flag;
    float index_coeff;
    float song_pos_coeff;

	if( x->l_obj.z_disabled ) // if object is disabled
		return ret_val;
	if( !x->l_buf || x->l_buf == (void *)0xbaadf00d )
		goto zero;		
	if(!x->l_buf->b_valid ) // if invalid buffer
		goto zero;

	// remember inuse state
    x->saveinuse		= x->l_buf->b_inuse;
	// set inuse to true for dsp block
	x->l_buf->b_inuse	= true;
	// address of buffer samples
	buffer 				= x->l_buf->b_samples;			
    
    x_read_coeff = 1.0f/(x->l_buf->b_frames*x->l_buf->b_nchans);
	flag = CLOCK_OK;
    index_coeff = (1.0f/x->slice_n);
    song_pos_coeff = 1.0f/( ((float)x->l_buf->b_frames)*x->b_list->size() );

	/* ====== DSP stuff ===== */
	if( (x->l_buf->b_nchans < 1)||(x->l_buf->b_nchans > 2) )
		goto zero;
	else if(x->b_list->size() < 1)
		goto zero;

	if(x->go == false)
		goto zero;

	while( n-- ) { // while decrementing vector exists...
		(*pos_out++) = x->read*x_read_coeff;
		(*total_pos_out++) = clock_count(&x->measure_clock) \
				/ ( (float) clock_time(&x->measure_clock) );
		(*song_pos_out++) = (
				(x->measure_index*x->l_buf->b_frames)
				+ clock_count(&x->measure_clock)
			) * song_pos_coeff;
		(*beat_clock_out++) = (float) clock_count(&x->beat_clock);
            
		clock_process(&x->q_clock, &flag);
		
		if(flag == CLOCK_ALARM) { //quant trigger
			clock_reset(&x->q_clock);
			clock_flag beat;
			clock_process(&x->beat_clock, &beat);
			if(beat == CLOCK_ALARM)
				clock_reset(&x->beat_clock);

			if(x->retrigger) { 
				x->retrigger = false;
				x->index = (long)*in; // index is signal    		
				x->index = ( (x->index < 0)||(x->index >= x->slice_n) ) ? 0. : x->index; 
				x->read = (unsigned long)( 
					(index_coeff*(float)x->index)
					* (x->l_buf->b_frames*x->l_buf->b_nchans) 
				);

			}            
			else if(x->follow) {
                x->index = ( (x->index < 0)||(x->index >= x->slice_n) ) ? 0. : x->index; 
                x->read = (unsigned long)( clock_count(&x->measure_clock)*(x->l_buf->b_nchans) );

			} /* retrigger or follow */
		} /* if alarm */

		if(x->l_buf->b_nchans == 1)
			(*out1++) = (*out0++) = buffer[x->read++];		// output index of buffer
		else if(x->l_buf->b_nchans == 2) {            
			(*out0++) = buffer[x->read++];
			(*out1++) = buffer[x->read++];
		}
        
        in++;
		if(x->read >= (unsigned long)(x->l_buf->b_frames*x->l_buf->b_nchans) ) {
			x->read -= (x->l_buf->b_frames*x->l_buf->b_nchans);
		}

		clock_process(&x->measure_clock, &flag);
		if(flag == CLOCK_ALARM) {
			increment_buffer(x);
			x_read_coeff = 1.0f/(x->l_buf->b_frames*x->l_buf->b_nchans);
			x->read = 0; //x->l_buf->b_frames;
		}
	} /* while */	
	/* === END DSP stuff ===== */

	x->l_buf->b_inuse = x->saveinuse;			// restore inuse state
	
	return ret_val;

zero:
	while ( n-- ) (*out0++) = (*out1++) = (*pos_out++) = 0.;
	return ret_val;
}

void *biddler_new( t_symbol *s, long chan ) {
	post("biddler_new");
	t_biddler *x = (t_biddler *)newobject( biddler_class );
	
	clock_init(&x->q_clock);
	clock_init(&x->measure_clock);
	clock_init(&x->beat_clock);
	x->b_list = new std::list<t_symbol *>;
	x->current = new std::list<t_symbol *>::iterator;
	intin( (t_object *)x, TIMEBASE );
	floatin( (t_object *)x, TEMPO );
	intin( (t_object *)x, JUMP );
	intin( (t_object *)x, QUANT_N );
	intin( (t_object *)x, SLICE_N );
	dsp_setup((t_pxobject *)x, 2);				// create sig inlets 
    
	outlet_new((t_pxobject *)x, "signal");			 
	outlet_new((t_pxobject *)x, "signal");			 
	outlet_new((t_pxobject *)x, "signal");				 
	outlet_new((t_pxobject *)x, "signal");
	outlet_new((t_pxobject *)x, "signal");
	outlet_new((t_pxobject *)x, "signal");
	x->l_sym = s;					// store buffer name argument
	x->filter = NULL;

/*    
    x->bpm = 120;
      x->tempo = 120;
    x->timebase = 0;
*/

	x->read = 0;
	x->retrigger = true;
	x->quant_n = 16;
	x->measure_index = 0;
	x->read = 0;
	x->slice_n = 8;
	x->measure_index_offset = 0;
	clock_set_time(&x->beat_clock, x->quant_n);
	x->l_buf = NULL;
	x->l_sym = NULL;
	x->follow = false;
	x->go = true;
	return x;
}

void biddler_free( t_biddler *x ) {
	if(x->b_list)
		delete x->b_list;
	if(x->current)
		delete x->current;
	dsp_free( (t_pxobject *)x );			// msp free function
}

void biddler_add( t_biddler *x, t_symbol *s ) { // sets the buffer~ to access
	t_buffer *b;					// pointer to a buffer
	post("added buffer");
	//x->l_sym = s;					// store name of buffer
	if(x->filter)
		if( std::string(x->filter->s_name) == std::string(s->s_name) )
			return;

    if ( ( b = (t_buffer *)( s->s_thing ) ) && ( ob_sym( b ) == ps_buffer ) ) { // if buffer is valid
		x->b_list->push_back(s);
		if(x->b_list->size() == 1) {
			x->l_buf = b;
			x->l_sym = s;
			x->filter = s;
			*x->current = x->b_list->begin();
			
			clock_set_time(&x->measure_clock, x->l_buf->b_frames);
			clock_reset(&x->measure_clock);
			clock_set_time(&x->q_clock, x->l_buf->b_frames/x->quant_n);
			clock_reset(&x->q_clock);
		} /* size == 1? */
    }
	else {
		error( "biddler~: no buffer~ %s", s->s_name ); // throw error
		//x->l_buf = 0;				// store null location	
    }
}

void biddler_follow(t_biddler *x, long n ) {
	if(n < 1)
		x->follow = false;
	else
		x->follow = true;
}

void biddler_go(t_biddler *x, long n ) {
	if(n < 1)
		x->go = false;
	else
		x->go = true;
}

void biddler_reset_position(t_biddler *x) {
	if((*x->current) == NULL)
		return;
	(*x->current) = x->b_list->begin();
	x->measure_index = 0;
	
	x->l_sym = *(*x->current);
	if(x->l_sym->s_thing) {
		x->l_buf = (t_buffer *)( x->l_sym->s_thing );
		clock_set_time(&x->measure_clock, x->l_buf->b_frames);
		clock_reset(&x->measure_clock);
		clock_set_time(&x->q_clock, x->l_buf->b_frames/x->quant_n);
		clock_reset(&x->q_clock);
	}

	x->measure_index_offset = 1;
}

// double click to view buffer~
void biddler_dblclick( t_biddler *x ) { 
	t_buffer *b;					// buffer address
	//t_symbol a = (*(*(*x->current) )).s_thing;
	if( ( b = (t_buffer *) ( x->l_sym->s_thing ) ) && ob_sym( b ) == ps_buffer )
		mess0( (t_object *) b, gensym( "dblclick" ) ); // view if buffer is valid
}

void biddler_assist( t_biddler *x, void *b, long m, long a, char *s ) {
	if(m == 1) {				// 1 = inlets
		switch (a) {			// inlet number switch
		case SIG_INDEX:				// sample index (0, slicing n)
			sprintf(s, "(signal) sample index (0, slicing n)");
			break;			// assist message
		case RETRIGGER: 
			sprintf(s, "(signal) retrigger at index");
			break;
		case SLICE_N: 
			sprintf(s, "(int) slicing n > 2");
			break;
		case QUANT_N: 
			sprintf(s, "(int) quantization (0, 64)");
			break;
		case JUMP:
			sprintf(s, "(int) jump measure. negative goes backward. positive goes forward.");
			break;
		case TEMPO: 
			sprintf(s, "(float) tempo");
			break;    
		case TIMEBASE:
			sprintf(s, "(int) timebase");
			break;
		default:
			break;
		}
	}
	else {						// 2 = outlets
		switch (a) {				// outlet number switch
		case 0:				// outlet 0
			sprintf(s, "(signal) Output L");
			break;			// assist message
		case 1:
			sprintf(s, "(signal) Output R");
			break;			// assist message
		case 2:
			sprintf(s, "(signal) slice position within measure");
			break;			// assist message
		case 3:
			sprintf(s, "(signal) position within measure");
			break;			// assist message
		case 4:
			sprintf(s, "(signal) beat clock");
			break;			// assist message
		case 5:
			sprintf(s, "(signal) song position");
			break;			// assist message
		default:
			break;
		}
	} /* inlets or outlets */

}

void increment_buffer(t_biddler *x) {
	//todo: check for bad ptrs so we can handle the disappearance of buffers
	
	for(int i = 0; i < abs(x->measure_index_offset); i++) {
        if(x->measure_index_offset >= 0) {
			(*x->current)++;    
			x->measure_index++;
		}
        else {
			(*x->current)--;    
			x->measure_index--; 
		}

        if( (*x->current) == x->b_list->end() ) {
			(*x->current) = x->b_list->begin();
			x->measure_index = 0;
			break;
		}
		if( ((x->measure_index) >= x->b_list->size()) || (x->measure_index < 0 ) ) {
			(*x->current) = x->b_list->begin();
			x->measure_index = 0;
		}
    
	} /* for */

	x->l_sym = *(*x->current);
	x->l_buf = (t_buffer *)( x->l_sym->s_thing );
	clock_set_time(&x->measure_clock, x->l_buf->b_frames);
	clock_reset(&x->measure_clock);
	clock_set_time(&x->q_clock, x->l_buf->b_frames/x->quant_n);
	clock_reset(&x->q_clock);
	x->measure_index_offset = 1;
}

void set_slice_n(t_biddler *x, long val) {
    x->slice_n = val;
}

void set_quant_n(t_biddler *x, long val) {
    if(val < 1) {
		x->quant_n = 0;
		return;
	}
	else
		x->quant_n = val;
    
	if(x->l_buf == (void *)0xbaadf00d)
		return;
	if(x->l_buf == NULL)
		return;
	clock_set_time(&x->q_clock, x->l_buf->b_frames/val);
    
	clock_set_time(&x->beat_clock, val);
}

void set_jump(t_biddler *x, long val) {
	x->measure_index_offset = val;
}
void set_tempo_float(t_biddler *x, float val) {
	//x->tempo = val;
}

void set_timebase(t_biddler *x, long val) {
	//x->timebase = val;
}
void set_bpm(t_biddler *x, long val) {
	//x->bpm = 0;
}

void bang(t_biddler *x) {
	x->retrigger = true;    
}
