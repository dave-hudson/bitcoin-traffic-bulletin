CC := gcc
CFLAGS := -std=gnu99 -O2 -Wall
LDFLAGS := -std=gnu99
RM := rm

#
# Define our target app.
#
APP := btb

#
# Define the source files for our build.
#
BTB_SRCS := btb.c

#
# Create a list of object files from source files.
#
BTB_OBJS := $(patsubst %.c,%.o,$(filter %.c,$(BTB_SRCS)))

BTB_LIBS := -lm

%.o : %.c
	$(CC) $(CFLAGS) -MD -c $< -o $@

.PHONY: all

all: $(APP)

btb: $(BTB_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(BTB_OBJS) $(BTB_LIBS)

-include $(BTB_OBJS:.o=.d) 

.PHONY: clean

clean:
	$(RM) -f $(APP) *.o $(patsubst %,%/*.o,$(SUBDIRS))
	$(RM) -f $(APP) *.d $(patsubst %,%/*.d,$(SUBDIRS))

.PHONY: realclean

realclean: clean
	$(RM) -f *~ $(patsubst %,%/*~,$(SUBDIRS))
	$(RM) -f *.txt $(patsubst %,%/*~,$(SUBDIRS))

