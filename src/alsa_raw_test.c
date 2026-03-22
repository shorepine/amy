#include <stdio.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

/*
    gcc -c alsa_raw_test.c
    gcc -o alsa_raw_test alsa_raw_test.o -lasound
*/

#define MF_FIX_NOTEOFF	0x01		// always translate [9n,kk,00] to [8n,kk,64]
#define MF_EMU_REL_VEL	0x02		// synthesize rel velocity (so [9n,kk,00]->[8n,kk,vv], real [8n,kk,vv] is unaffected)
#define MF_FWD_ACTSENS	0x04		// forward 0xFE active sensing RT messages (blocked by default)
#define MF_TX_MODIFIED	0x08		// send modified data to MIDI output port

#define MF_FLAG_MASK	0x0F

snd_rawmidi_t *midi_in  = NULL;
snd_rawmidi_t *midi_out = NULL;

pthread_t midi_thread;

#define BUFFER_SIZE	256
#define SYSEX_MAX_CPL	16
#define EMU_RVEL_DAMP	3

int mf_flags = MF_FIX_NOTEOFF;
int thread_should_quit = 0;

uint8_t buffer[BUFFER_SIZE];
uint8_t rsts = 0x80;

int rstat_flag = 0;
int sysex_flag = 0;
int rxbyte_cnt = 0;
int sigma_rvel = 0;

int running = 0;

int fix_noteoff, emu_rel_vel, fwd_actsens, tx_modified;

int midi_write(uint8_t *, int);

int midi_open(void)
{
    // "virtual" device string creates the bidirectional sequencer bridge
    int err = snd_rawmidi_open(&midi_in, &midi_out, "virtual", 0);
    if (err < 0) {
	fprintf(stderr, "Could not create virtual raw port: %s\n", snd_strerror(err));
	return -1;
    } else {
	fix_noteoff = mf_flags & MF_FIX_NOTEOFF;
	emu_rel_vel = mf_flags & MF_EMU_REL_VEL;
	fwd_actsens = mf_flags & MF_FWD_ACTSENS;
	tx_modified = mf_flags & MF_TX_MODIFIED;
    }
    return err;
}

void midi_close(void)
{
    if (midi_in ) { snd_rawmidi_close(midi_in ); midi_in  = NULL; }
    if (midi_out) { snd_rawmidi_close(midi_out); midi_out = NULL; }
}

int midi_setflags(int newflg)
{
    int oldflg = mf_flags;
    mf_flags = newflg & MF_FLAG_MASK;
    fix_noteoff = mf_flags & MF_FIX_NOTEOFF;
    emu_rel_vel = mf_flags & MF_EMU_REL_VEL;
    fwd_actsens = mf_flags & MF_FWD_ACTSENS;
    tx_modified = mf_flags & MF_TX_MODIFIED;
    return oldflg;
}

#define EMU_RVEL_ROUND	(1 << (EMU_RVEL_DAMP - 1))

#define midi_get(x)	{ x = *buf++; rem--; }

int _upd_sigma(int vel)
{
    // emulate release velocity for decoded [9n,kk,vv]->[8n,kk,vv] events
    // (which are usually fixed at 64) by applying an exponential moving
    // average and rounding to the most recently received velocity values
    int scl = (sigma_rvel + EMU_RVEL_ROUND) >> EMU_RVEL_DAMP;
    sigma_rvel = sigma_rvel + vel - scl;
    scl = (sigma_rvel + EMU_RVEL_ROUND) >> EMU_RVEL_DAMP;
    return scl;
}

int _midi_echo(uint8_t d0, uint8_t d1, uint8_t d2, int len)
{
    uint8_t tbuf[4];
    if ((len < 0) || (len > 3)) return 0;
    tbuf[1] = tbuf[2] = tbuf[3] = 0;
    if (len > 2) tbuf[2] = d2;
    if (len > 1) tbuf[1] = d1;
    if (len > 0) tbuf[0] = d0;
    if (tx_modified)
	return midi_write(&tbuf[0], len);
    else return len;
}

#define midi_echo0		_midi_echo(0xFE,0,0,1)
#define midi_echo1(a)		_midi_echo(a,0,0,1)
#define midi_echo2(a,b)		_midi_echo(a,b,0,2)
#define midi_echo3(a,b,c)	_midi_echo(a,b,c,3)

int midi_parse(uint8_t *buf, int len)
{
    uint8_t dat0, dat1, dat2;
    int cmd, chn, rem, erv;
    rem = len;
    while (rem > 0) {
	midi_get(dat0);
	if (sysex_flag) {
	    if ((dat0 & 0x80) && (dat0 < 0xF7)) {
		fprintf(stderr, "\nWARNING : value=%02X - channel of system common data inside SysEx block!\n", dat0);
		continue;
	    } else {
		if (dat0 < 0x80) {
		    fprintf(stdout, " %02Xh", dat0); fflush(stdout);
		    if (++rxbyte_cnt == SYSEX_MAX_CPL) { fprintf(stdout, "\n"); rxbyte_cnt=0; }
		    midi_echo1(dat0); continue;
		} else {
		    if ((dat0 == 0xF7) && (rxbyte_cnt > 0)) fprintf(stdout, "\n");
		}
	    }
	} else {
	    if      (dat0 < 0x80) { dat1 = dat0;    dat0 = rsts; rstat_flag=1; }
	    else if (dat0 < 0xF0) { midi_get(dat1); rsts = dat0; rstat_flag=0; }
	}
	cmd = dat0 & 0xF0; chn = dat0 & 0x0F;
	if (rstat_flag) fprintf(stdout, " $ ");
	else       fprintf(stdout, " : ");
	switch (cmd) {
	  case 0x80:
	    midi_get(dat2); erv=_upd_sigma(dat2);
	    fprintf(stdout, "\tNoteOff : chn=%02d key=%03d vel=%03d\t(%03d)\n", chn, dat1, dat2, erv);
	    midi_echo3(dat0,dat1,dat2); break;
	  case 0x90:
	    midi_get(dat2);
	    if (dat2) {
		erv=_upd_sigma(dat2);
		fprintf(stdout,  "\tNoteOn  : chn=%02d key=%03d vel=%03d\t(%03d)\n", chn, dat1, dat2, erv);
		midi_echo3(dat0,dat1,dat2); 
	    } else {
		if (fix_noteoff) { dat0 &= 0xEF; dat2 = 64; fprintf(stdout, "! ", dat0); }
		erv=_upd_sigma(dat2); if (emu_rel_vel) dat2 = erv;
		fprintf(stdout, "\tNote0ff : chn=%02d key=%03d vel=%03d\t(%03d)\n", chn, dat1, dat2, erv);
		midi_echo3(dat0,dat1,dat2); 
	    }
	    break;
	  case 0xA0:
	    midi_get(dat2); fprintf(stdout, "\tKeyPres : chn=%02d key=%03d val=%03d\n", chn, dat1, dat2);
	    midi_echo3(dat0,dat1,dat2); break;
	  case 0xB0:
	    midi_get(dat2); fprintf(stdout, "\tControl : chn=%02d ctl=%03d val=%03d\n", chn, dat1, dat2);
	    if ((dat1 == 0) && (dat2 == 127)) running = 0; // <- quit request
	    midi_echo3(dat0,dat1,dat2); break;
	  case 0xC0:
	    fprintf(stdout, "\tProgram : chn=%02d prg=%03d\n", chn, dat1);
	    midi_echo2(dat0,dat1); break;
	  case 0xD0:
	    fprintf(stdout, "\tChnPres : chn=%02d val=%03d\n", chn, dat1);
	    midi_echo2(dat0,dat1); break;
	  case 0xE0:
	    midi_get(dat2); fprintf(stdout, "\tPBender : chn=%02d val=+%04d\n", chn, ((dat2 << 7) | dat1) - 8192);
	    midi_echo3(dat0,dat1,dat2); break;
	  case 0xF0:
	    if (dat0 == 0xF2) midi_get(dat1);
	    switch(dat0) {
	      case 0xF0:
		fprintf(stdout, "  Sys Ex  : --- System Exclusive block BEGIN ---\n"); sysex_flag=1; rxbyte_cnt=0;
		midi_echo1(dat0); break;
	      case 0xF1:
		midi_get(dat1); fprintf(stdout, "\tSys Com : MTC Quarter Frame - T=%d V=%02d\n", dat1 >> 4, dat1 & 15);
		midi_echo2(dat0,dat1); break;
	      case 0xF2:
		midi_get(dat2); fprintf(stdout, "\tSys Com : Song Position Pointer - pos=%05d\n", (dat2 << 7) | dat1);
		midi_echo3(dat0,dat1,dat2); break;
	      case 0xF3:
		midi_get(dat1); fprintf(stdout, "\tSys Com : Song Select - song #%03d\n", dat1);
		midi_echo2(dat0,dat1); break;
	      case 0xF4: fprintf(stdout, "\tSys Com : reserved 0xF4\n");  break;
	      case 0xF5: fprintf(stdout, "\tSys Com : reserved 0xF5\n");  break;
	      case 0xF6:
		fprintf(stdout, "\tSys Com : Tune Request\n");
		midi_echo1(dat0); break;
	      case 0xF7:
		fprintf(stdout, "  Sys Ex  : ---- System Exclusive block END ----\n"); sysex_flag=0;
		midi_echo1(dat0); break;
	      case 0xF8:
		fprintf(stdout, "\tSys RT  : MIDI clock\n");
		midi_echo1(dat0); break;
	      case 0xF9: fprintf(stdout, "\tSys RT  : reserved 0xF9\n");  break;
	      case 0xFA:
		fprintf(stdout, "\tSys RT  : MIDI start\n");
		midi_echo1(dat0); break;
	      case 0xFB:
		fprintf(stdout, "\tSys RT  : MIDI continue\n");
		midi_echo1(dat0); break;
	      case 0xFC:
		fprintf(stdout, "\tSys RT  : MIDI stop\n");
		midi_echo1(dat0); break;
	      case 0xFD: fprintf(stdout, "\tSys RT  : reserved 0xFD\n");  break;
	      case 0xFE:
		fprintf(stdout, "\tSys RT  : Active Sensing\n");
		if (fwd_actsens) midi_echo1(dat0); break;
	      case 0xFF:
		fprintf(stdout, "\tSys RT  : MIDI reset\n");
		midi_echo1(dat0); break;
	      default: fprintf(stderr, "ERROR   : sysex=%02X - this shouldn't happen!\n", dat0); break;
	    }
	    break;
	  default: fprintf(stderr, "ERROR  : value=%02X - this shouldn't happen!\n", dat0); break;
	}
    }
    return rem;
}

int midi_poll()
{
    int count = snd_rawmidi_poll_descriptors_count(midi_in);
    struct pollfd *pfds = alloca(sizeof(struct pollfd) * count);
    snd_rawmidi_poll_descriptors(midi_in, pfds, count);
    // Timeout of 0 for immediate check, or -1 to wait
    if (poll(pfds, count, 0) > 0) {
	unsigned short revents;
	snd_rawmidi_poll_descriptors_revents(midi_in, pfds, count, &revents);
	if (revents & POLLIN) return 1;
    }
    return 0;
}

int midi_read(uint8_t *buf, int len)
{
    return snd_rawmidi_read(midi_in, buf, len);
}

int midi_write(uint8_t *buf, int len)
{
    return snd_rawmidi_write(midi_out, buf, len);
}

void midi_flush()
{
    while (midi_poll())
	midi_read(&buffer[0], BUFFER_SIZE);
}

void *midi_thread_func(void *arg)
{
    uint8_t buffer[256];
    while (!thread_should_quit) {
	ssize_t n = snd_rawmidi_read(midi_in, buffer, sizeof(buffer));
	if (n > 0) midi_parse(buffer, n);
    }
    return NULL;
}

void run_midi()
{
    int result;
    result = pthread_create(&midi_thread, NULL, midi_thread_func, NULL);
    if (result) {
	fprintf(stderr, "Error creating thread\n");
	exit(1);
    }
}

void stop_midi()
{
    thread_should_quit = 1;
    sleep(1);
    pthread_join(midi_thread, NULL);
}

int main(int argc, char *argv[])
{
    int n;
    n = midi_open();
    if (n < 0) return -1;
    midi_flush();
    midi_setflags(MF_TX_MODIFIED | MF_EMU_REL_VEL | MF_FIX_NOTEOFF);
#if 0
    // using port polling for rx
    while (1) {
	if (midi_poll()) {
	    n = midi_read(&buffer[0], BUFFER_SIZE);
	    midi_parse(&buffer[0], n);
	} else {
	    usleep(250L);
	}
    }
#else
    // using separate thread for rx
    run_midi();
    fprintf(stdout, "running - send BankHi=127 to exit...\n");
    running = 1;
    while (running);
    fprintf(stdout, "CC(0,0,127) received - exiting\n");
    stop_midi();
#endif
    midi_close();
    return 0;
}
