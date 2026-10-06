/* Construction pure des réglages termios d'un port série. */

#include "serial_termios.h"

void
serial_termios_apply(struct termios *tio, gint bits, gint parity, gint stopbits, gint flow)
{
	tio->c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CRTSCTS
	tio->c_cflag &= ~CRTSCTS;
#endif
#ifdef CRTS_IFLOW
	tio->c_cflag &= ~CRTS_IFLOW;
#endif
#ifdef CCTS_OFLOW
	tio->c_cflag &= ~CCTS_OFLOW;
#endif

	switch(bits)
	{
	case 5:
		tio->c_cflag |= CS5;
		break;
	case 6:
		tio->c_cflag |= CS6;
		break;
	case 7:
		tio->c_cflag |= CS7;
		break;
	default:
		tio->c_cflag |= CS8;
		break;
	}

	if(parity == 1)
		tio->c_cflag |= PARENB | PARODD;
	else if(parity == 2)
		tio->c_cflag |= PARENB;

	if(stopbits == 2)
		tio->c_cflag |= CSTOPB;

	tio->c_cflag |= CREAD | CLOCAL;
	tio->c_iflag = IGNPAR | IGNBRK;

	if(flow == 1)
		tio->c_iflag |= IXON | IXOFF;
	else if(flow == 2)
	{
#ifdef CRTSCTS
		tio->c_cflag |= CRTSCTS;
#endif
#ifdef CRTS_IFLOW
		tio->c_cflag |= CRTS_IFLOW;
#endif
#ifdef CCTS_OFLOW
		tio->c_cflag |= CCTS_OFLOW;
#endif
	}

	tio->c_oflag = 0;
	tio->c_lflag = 0;
	tio->c_cc[VTIME] = 0;
	tio->c_cc[VMIN] = 1;
}
