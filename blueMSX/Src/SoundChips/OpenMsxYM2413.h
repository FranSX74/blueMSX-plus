// This file is taken from the openMSX project. 
// The file has been modified to be built in the blueMSX environment.

#ifndef __YM2413_HH__
#define __YM2413_HH__

#include <string>

using namespace std;


typedef unsigned long  EmuTime;
typedef unsigned char  omsx_byte;
typedef unsigned short word;

extern "C" {
#include "AudioMixer.h"
}


#ifndef OPENMSX_SOUNDDEVICE
#define OPENMSX_SOUNDDEVICE

class SoundDevice
{
	public:
        SoundDevice() : internalMuted(true) {}
		void setVolume(short newVolume) {
	        setInternalVolume(newVolume);
        }

	protected:
		virtual void setInternalVolume(short newVolume) = 0;
        void setInternalMute(bool muted) { internalMuted = muted; }
        bool isInternalMuted() const { return internalMuted; }
	public:
		virtual void setSampleRate(int newSampleRate, int Oversampling) = 0;
		virtual int* updateBuffer(int length) = 0;

	private:
		bool internalMuted;
};

#endif

#ifndef OPENMSX_YM2413BASE
#define OPENMSX_YM2413BASE

class OpenYM2413Base : public SoundDevice
{
public:
    OpenYM2413Base() {}
    virtual ~OpenYM2413Base() {}
		
	virtual void reset(const EmuTime &time) = 0;
	virtual void writeReg(omsx_byte r, omsx_byte v, const EmuTime &time) = 0;
    virtual omsx_byte peekReg(omsx_byte r) = 0;
	
	virtual int* updateBuffer(int length) = 0;
	virtual void setSampleRate(int sampleRate, int Oversampling) = 0;

    virtual void loadState() = 0;
    virtual void saveState() = 0;
};

#endif

class Slot
{
	public:
		Slot();

		inline int volume_calc(omsx_byte LFO_AM);
		inline void KEY_ON (omsx_byte key_set);
		inline void KEY_OFF(omsx_byte key_clr);

		omsx_byte ar;	// attack rate: AR<<2
		omsx_byte dr;	// decay rate:  DR<<2
		omsx_byte rr;	// release rate:RR<<2
		omsx_byte KSR;	// key scale rate
		omsx_byte ksl;	// keyscale level
		omsx_byte ksr;	// key scale rate: kcode>>KSR
		omsx_byte mul;	// multiple: mul_tab[ML]

		// Phase Generator
		int phase;	// frequency counter
		int freq;	// frequency counter step
		omsx_byte fb_shift;	// feedback shift value
		int op1_out[2];	// slot1 output for feedback

		// Envelope Generator
		omsx_byte eg_type;	// percussive/nonpercussive mode
		omsx_byte state;	// phase type
		int TL;		// total level: TL << 2
		int TLL;	// adjusted now TL
		int volume;	// envelope counter
		int sl;		// sustain level: sl_tab[SL]

		omsx_byte eg_sh_dp;	// (dump state)
		omsx_byte eg_sel_dp;	// (dump state)
		omsx_byte eg_sh_ar;	// (attack state)
		omsx_byte eg_sel_ar;	// (attack state)
		omsx_byte eg_sh_dr;	// (decay state)
		omsx_byte eg_sel_dr;	// (decay state)
		omsx_byte eg_sh_rr;	// (release state for non-perc.)
		omsx_byte eg_sel_rr;	// (release state for non-perc.)
		omsx_byte eg_sh_rs;	// (release state for perc.mode)
		omsx_byte eg_sel_rs;	// (release state for perc.mode)

		omsx_byte key;	// 0 = KEY OFF, >0 = KEY ON

		// LFO
		omsx_byte AMmask;	// LFO Amplitude Modulation enable mask
		omsx_byte vib;	// LFO Phase Modulation enable flag (active high)

		int wavetable;	// waveform select
};

class Channel
{
	public:
		Channel();
		inline int chan_calc(omsx_byte LFO_AM);
		inline void CALC_FCSLOT(Slot *slot);

		Slot slots[2];
		// phase generator state
		int block_fnum;	// block+fnum
		int fc;		// Freq. freqement base
		int ksl_base;	// KeyScaleLevel Base step
		omsx_byte kcode;	// key code (for key scaling)
		omsx_byte sus;	// sus on/off (release speed in percussive mode)
};

class OpenYM2413 : public OpenYM2413Base
{
	public:
		OpenYM2413(const string &name, short volume, const EmuTime &time);
		virtual ~OpenYM2413();
		
		virtual void reset(const EmuTime &time);
		virtual void writeReg(omsx_byte r, omsx_byte v, const EmuTime &time);
        virtual omsx_byte peekReg(omsx_byte r) { return regs[r]; }
		
		virtual void setInternalVolume(short newVolume);
		virtual int* updateBuffer(int length);
		virtual void setSampleRate(int sampleRate, int Oversampling);

        virtual void loadState();
        virtual void saveState();

	private:
        int filter(int input);
		void checkMute();
		bool checkMuteHelper();
		
		void init_tables();
		
		inline void advance_lfo();
		inline void advance();
		inline int rhythm_calc(bool noise);

		inline void set_mul(omsx_byte slot, omsx_byte v);
		inline void set_ksl_tl(omsx_byte chan, omsx_byte v);
		inline void set_ksl_wave_fb(omsx_byte chan, omsx_byte v);
		inline void set_ar_dr(omsx_byte slot, omsx_byte v);
		inline void set_sl_rr(omsx_byte slot, omsx_byte v);
		void load_instrument(omsx_byte chan, omsx_byte slot, omsx_byte* inst);
		void update_instrument_zero(omsx_byte r);
		void setRhythmMode(bool newMode);

        int buffer[AUDIO_MONO_BUFFER_SIZE];
        int oplOversampling;

        int in[5];

        omsx_byte regs[0x40];

		Channel channels[9];	// OPLL chips have 9 channels
		omsx_byte instvol_r[9];		// instrument/volume (or volume/volume in percussive mode)

        short maxVolume;

		unsigned int eg_cnt;		// global envelope generator counter
		unsigned int eg_timer;		// global envelope generator counter works at frequency = chipclock/72
		unsigned int eg_timer_add;	    	// step of eg_timer

		bool rhythm;			// Rhythm mode

		// LFO
		unsigned int lfo_am_cnt;
		unsigned int lfo_am_inc;
		unsigned int lfo_pm_cnt;
		unsigned int lfo_pm_inc;

		int noise_rng;		// 23 bit noise shift register
		int noise_p;		// current noise 'phase'
		int noise_f;		// current noise period

		// instrument settings
		//   0     - user instrument
		//   1-15  - fixed instruments
		//   16    - bass drum settings
		//   17-18 - other percussion instruments
		omsx_byte inst_tab[19][8];

		int fn_tab[1024];		// fnumber->increment counter

		omsx_byte LFO_AM;
		omsx_byte LFO_PM;
};

#endif

