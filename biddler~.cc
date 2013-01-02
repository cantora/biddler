#include "ext.h"
#include "z_dsp.h"
#include "ext_critical.h"
#include "buffer.h"
#include "ext_common.h"
#include "clock.h"
#include <vector>
#include <string>

void		*biddler_class;			// this object
t_symbol	*ps_buffer;			// a buffer 

typedef struct _biddler				// biddler data structure
{	
	t_pxobject l_obj;			// object info struct

	t_symbol *first_sym;

	unsigned long slice_n;
	unsigned long quant_n;
	long measure_index;
	long measure_index_offset;
	unsigned long read;
	bool retrigger;
	bool follow;
	bool go;
	struct clock q_clock;
	struct clock measure_clock;
	struct clock beat_clock;
	std::vector<t_symbol *> *sym_arr;
	int reset_buffer;   
	int update_q_clock;	
	int error;
} t_biddler;

enum inlets {
	SIG_INDEX = 0,
	RETRIGGER,
	SLICE_N,
	QUANT_N,
	JUMP,
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

void *biddler_new(t_symbol *s, long chan );		// makes a new biddler~
void biddler_error(t_biddler *x, char *s);
void biddler_reset_state(t_biddler *x);
void biddler_free( t_biddler *x );			// deletes biddler~
void biddler_dsp (t_biddler *x, t_signal **sp );	// sets dsp for biddler~
t_int *biddler_perform( t_int *w );			// biddler dsp perform
//void biddler_dblclick( t_biddler *x );			// double-click handler
void biddler_assist( t_biddler *x, void *b, long m, long a, char *s );
void biddler_add( t_biddler *x, t_symbol *s );		// 'set' message handler
//void biddler_clear( t_biddler *x);

void set_slice_n(t_biddler *x, long val);
void set_quant_n(t_biddler *x, long val);
void set_jump(t_biddler *x, long val);
void bang(t_biddler *x);
void increment_buffer(t_biddler *x);
void biddler_follow(t_biddler *x, long n );
void biddler_go(t_biddler *x, long n );
void biddler_reset_position(t_biddler *x);

int main(void) {
	setup( (t_messlist **)&biddler_class, (method)biddler_new, (method)biddler_free, 
		(short)sizeof(t_biddler), 0L, 0 );
	addmess( (method)biddler_dsp, "dsp", A_CANT, 0 );	
	addmess( (method)biddler_add, "add", A_SYM, 0 );	
	//addmess( (method)biddler_clear, "clear", A_NOTHING, 0 );	
	addmess( (method)biddler_follow, "follow", A_LONG, 0 );		
	addmess( (method)biddler_go, "go", A_LONG, 0 );		// 
	addmess( (method)biddler_reset_position, "reset_position", A_NOTHING, 0 );
	
	addinx((method)set_jump, JUMP);
	addinx((method)set_quant_n, QUANT_N);
	addinx((method)set_slice_n, SLICE_N);
	
	addbang((method)bang);
	addmess( (method)biddler_assist, "assist", A_CANT, 0 );	// add assist method
	//addmess( (method)biddler_dblclick, "dblclick", A_CANT, 0 );	// add double-click method
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

int current_buffer(t_biddler *x, t_buffer **buffer) {
	t_symbol *sym;

	if(x->error)
		return -1;

	if(x->measure_index >= (long) x->sym_arr->size() \
			|| x->measure_index < 0 ) {
		biddler_error(x, "measure index overflow");
		return -1;
	}

	sym = (*x->sym_arr)[x->measure_index];
	if(sym == NULL) {
		biddler_error(x, "biddler~: didnt expect sym to be null");
		return -1;
	}

	//post("current_buffer: %s", sym->s_name);

	*buffer = (t_buffer *)( sym->s_thing );
	if( !(*buffer) || *buffer == (void *)0xbaadf00d \
			|| !(*buffer)->b_valid || ( (*buffer)->b_nchans < 1 ) \
			|| ( (*buffer)->b_nchans > 2 ) ) {
		return -1;
	}

	return 0;
}

t_int *biddler_perform( t_int *w ) {
	t_float *in, *out0, *out1;
	t_float *pos_out, *total_pos_out;
	t_float *beat_clock_out, *song_pos_out;
	t_int *ret_val;
	int n;
	t_biddler *x;
	clock_flag flag;
	float index_coeff;
	unsigned long savedinuse;

	static float x_read_coeff;
	static float song_pos_coeff;
	
	static t_buffer *buffer = NULL;

	// inlet signal vector locations
	in				= (t_float *)(w[1 + SIG_INDEX]);
	// outlet signal vector locations
	out0			= (t_float *)(w[1 + NUM_SIG_INLETS+OUT_L]);		
	out1 			= (t_float *)(w[1 + NUM_SIG_INLETS+OUT_R]);		
	pos_out 		= (t_float *)(w[1 + NUM_SIG_INLETS+POS_OUT]);		
	total_pos_out 	= (t_float *)(w[1 + NUM_SIG_INLETS+TOTAL_POS_OUT]);		
	beat_clock_out 	= (t_float *)(w[1 + NUM_SIG_INLETS+BEAT_CLOCK_OUT]);	
	song_pos_out 	= (t_float *)(w[1 + NUM_SIG_INLETS+SONG_POS_OUT]);		
	
	// always return w plus one more than the 2nd argument
	ret_val = w + DSP_ADD_2_ARG + 1;  
	
	// signal vector size in samples
	n = (int)(w[DSP_ADD_2_ARG-1]);			
	// biddler struct location
	x = (t_biddler *)(w[DSP_ADD_2_ARG]);		
	
	if( x->l_obj.z_disabled || x->error \
			|| x->sym_arr->size() < 1 \
			|| x->go == false) {
		buffer = NULL;
		goto zero;
	}

	index_coeff = (1.0f/x->slice_n);
#define SAVE_INUSE() \
	do { \
		savedinuse = buffer->b_inuse; \
		buffer->b_inuse = true; \
	} while(0)

	if(buffer != NULL) {
		if(x->reset_buffer) {
			buffer = NULL;
			x->reset_buffer = 0;
		}
		else {
			SAVE_INUSE();
		}
	}
	/* ====== DSP stuff ===== */
	while( n-- ) { // while decrementing vector exists...
		if(buffer == NULL) {
			if(current_buffer(x, &buffer) != 0) { 
				post("biddler~: WARNING, invalid buffer, zeroing output.");
				buffer = NULL;
				goto zero;
			}
			
			SAVE_INUSE();
		
			clock_set_time(&x->measure_clock, buffer->b_frames);
			clock_reset(&x->measure_clock);
			clock_set_time(&x->q_clock, buffer->b_frames/x->quant_n);
			clock_reset(&x->q_clock);

			x->read = 0;
			x_read_coeff = 1.0f/(buffer->b_frames*buffer->b_nchans);
			/* we assume that all buffers in the list are the same size */
			song_pos_coeff = 1.0f/( ((float)buffer->b_frames)*x->sym_arr->size() );
			x->reset_buffer = 0;
		}

		if(x->update_q_clock) {
			clock_set_time(&x->q_clock, buffer->b_frames/x->quant_n);
			x->update_q_clock = 0;
		}

		(*pos_out++) = x->read*x_read_coeff;
		(*total_pos_out++) = clock_count(&x->measure_clock) \
				/ ( (float) clock_time(&x->measure_clock) );
		(*song_pos_out++) = (
				(x->measure_index*buffer->b_frames)
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
				long index;
				x->retrigger = false;
				index = (long) *in; // index is signal    		
				index = ( (index < 0)||(index >= ((long) x->slice_n)) ) ? 0. : index; 
				x->read = (unsigned long)( 
					(index_coeff*(float)index)
					* (buffer->b_frames*buffer->b_nchans) 
				);
			}            
			else if(x->follow) {
                x->read = (unsigned long)( clock_count(&x->measure_clock)*(buffer->b_nchans) );
			} /* retrigger or follow */
		} /* if alarm */

		if(buffer->b_nchans == 1)
			(*out1++) = (*out0++) = buffer->b_samples[x->read++];
		else if(buffer->b_nchans == 2) {
			(*out0++) = buffer->b_samples[x->read++];
			(*out1++) = buffer->b_samples[x->read++];
		}
        
		in++;
		if(x->read >= (unsigned long)(buffer->b_frames*buffer->b_nchans) ) {
			x->read -= (buffer->b_frames*buffer->b_nchans);
		}

		clock_process(&x->measure_clock, &flag);
		if(flag == CLOCK_ALARM) {
			increment_buffer(x);
			buffer->b_inuse = savedinuse;
			buffer = NULL;
		}
	} /* while */	
	/* === END DSP stuff ===== */

	if(buffer) 
		buffer->b_inuse = savedinuse;
	
	return ret_val;
zero:
	while ( n-- ) {
		(*out0++) = (*out1++) \
			= (*pos_out++) = (*total_pos_out) \
			= (*song_pos_out) = 0.;
	}
	return ret_val;
}

void *biddler_new( t_symbol *s, long chan ) {
	(void)(s);
	(void)(chan);

	post("biddler_new");
	t_biddler *x = (t_biddler *)newobject( biddler_class );
	
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
	
	x->sym_arr = new std::vector<t_symbol *>;
	if(x->sym_arr == NULL) {
		biddler_error(x, "biddler~: memory error");
	}

	clock_init(&x->q_clock);
	clock_init(&x->measure_clock);
	clock_init(&x->beat_clock);
	x->quant_n = 16;
	x->slice_n = 8;
	clock_set_time(&x->beat_clock, x->quant_n);
	x->follow = false;
	x->go = true;
	x->update_q_clock = 0;
	x->error = 0;

	biddler_reset_state(x);

	return x;
}

void biddler_free( t_biddler *x ) {
	if(x->sym_arr) 
		delete x->sym_arr;
	dsp_free( (t_pxobject *)x );			// msp free function
}

void biddler_error(t_biddler *x, char *s) {
	if(x->error == 0)
		error(s);
	x->error = 1;
}

void biddler_reset_state(t_biddler *x) {
	x->first_sym = NULL;

	x->retrigger = false;
	x->read = 0;
	biddler_reset_position(x);
}

void biddler_add(t_biddler *x, t_symbol *s) { // sets the buffer~ to access
	t_buffer *b;					// pointer to a buffer

	if(x->error)
		return;

	post("add buffer");
	if(x->first_sym)
		if( std::string(x->first_sym->s_name) == std::string(s->s_name) )
			return;

    if ( ( b = (t_buffer *)( s->s_thing ) ) && ( ob_sym( b ) == ps_buffer ) ) { // if buffer is valid
		x->sym_arr->push_back(s);
		if(x->sym_arr->size() == 1)
			x->first_sym = s;
    }
	else {
		biddler_error(x, "biddler~: invalid buffer added");
    }
}

/*void biddler_clear( t_biddler *x) {
	if(x->error)
		return;

	post("clear all buffers");
	//biddler_reset_state(x);
	x->sym_arr->clear();
}*/

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
	x->measure_index = 0;
	clock_reset(&x->measure_clock);
	clock_reset(&x->q_clock);
	clock_reset(&x->beat_clock);
	x->reset_buffer = 1;
	x->measure_index_offset = 1;
}

void biddler_assist( t_biddler *x, void *b, long m, long a, char *s ) {
	(void)(x);
	(void)(b);

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
	if(x->error)
		return;

	for(int i = 0; i < abs(x->measure_index_offset); i++) {
		if(x->measure_index_offset >= 0) {
			x->measure_index++;
		}
        else {
			x->measure_index--; 
		}

        if( x->measure_index < 0 || x->measure_index >= (long) x->sym_arr->size() ) {
			x->measure_index = 0;
			break;
		}
	} /* for */

	x->measure_index_offset = 1;
}

void set_slice_n(t_biddler *x, long val) {
	if(val < 2)
		x->slice_n = 2;
	else
		x->slice_n = val;
}

void set_quant_n(t_biddler *x, long val) {
    if(val < 2) 
		x->quant_n = 2;
	else
		x->quant_n = val;
    
	clock_set_time(&x->beat_clock, val);
	x->update_q_clock = 1;
}

void set_jump(t_biddler *x, long val) {
	x->measure_index_offset = val;
}

void bang(t_biddler *x) {
	x->retrigger = true;    
}
