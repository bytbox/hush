CFLAGS = -Wall -Werror -O0 -g
#ALSALIB = -lasound
LDFLAGS = ${ALSALIB}

all: hush

clean:
	${RM} hush

