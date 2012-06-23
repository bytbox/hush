CFLAGS = -Wall -Wunused-function -Wunused-parameter -Werror -O0 -g
#ALSALIB = -lasound
LDFLAGS = ${ALSALIB}

all: hush

clean:
	${RM} hush

