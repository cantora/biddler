INSTALL_DIR			= /Applications/MaxMSP\ 4.6/Cycling\ \'74/externals/
OSX_FRAMEWORK_DIR	= /Developer/SDKs/MacOSX10.4u.sdk/Library/Frameworks
#MAX_SDK_HOME		= ../../../lib/audio/maxmsp/maxmspUBSDK/c74support
MAX_SDK_HOME		= max_sdk

NAME			= biddler~
DEF				= $(NAME)
OSX_FRAMEWORK	= -F$(OSX_FRAMEWORK_DIR)
COMMON_FLAGS	= -arch i386 -mmacosx-version-min=10.4 \
					-isysroot /Developer/SDKs/MacOSX10.4u.sdk
LINK_FLAGS		= -bundle
FRAMEWORKS		= -framework MaxAPI -framework MaxAudioApi
COMPILER		= g++
INCLUDE			= -I$(MAX_SDK_HOME)/max-includes/ \
					-I$(MAX_SDK_HOME)/msp-includes/ \
					-include $(MAX_SDK_HOME)/max-includes/macho-prefix.h
#OPTIMIZE		= -Os
OPTIMIZE		= -ggdb
COMPILE_FLAGS	= -pipe -Wno-trigraphs -fpascal-strings -fasm-blocks \
					-fmessage-length=0 -Wall -Wextra -Wno-four-char-constants \
					-Wno-unknown-pragmas $(OPTIMIZE)
BUNDLE_DIR		= $(NAME).mxo/Contents


$(BUNDLE_DIR)/MacOS/$(NAME): $(NAME).o clock.o $(DEF).def $(BUNDLE_DIR)/MacOS Info.plist
	$(COMPILER) -o $@ $(OSX_FRAMEWORK) $(FRAMEWORKS) $(COMMON_FLAGS)  clock.o $(LINK_FLAGS) $(NAME).o $(DEF).def

clock.o: clock.c
	$(COMPILER) $(OSX_FRAMEWORK) $(COMMON_FLAGS) $(COMPILE_FLAGS) $(INCLUDE) -c $< -o $@

$(NAME).o: $(NAME).cc
	$(COMPILER) $(OSX_FRAMEWORK) $(COMMON_FLAGS) $(COMPILE_FLAGS) $(INCLUDE) -c $< -o $@

$(BUNDLE_DIR)/Info.plist: $(BUNDLE_DIR)/MacOS
	cp ./Info.plist $@

$(BUNDLE_DIR)/MacOS:
	mkdir -p $(BUNDLE_DIR)/MacOS

install: $(BUNDLE_DIR)/MacOS/$(NAME) $(BUNDLE_DIR)/Info.plist
	cp -r -i $(NAME).mxo/ $(INSTALL_DIR)/$(NAME).mxo

clean:
	rm -f *.o
	rm -rf $(NAME).mxo

