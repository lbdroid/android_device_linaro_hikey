#include <webrtc/modules/audio_processing/include/audio_processing.h>
#include <webrtc/modules/include/module_common_types.h>
#include "webrtc_wrapper.h"

#define TO_CPP(a) (reinterpret_cast<webrtc::AudioProcessing*>(a))
#define TO_C(a)   (reinterpret_cast<audioproc*>(a))
#define F_TO_CPP(a) (reinterpret_cast<webrtc::AudioFrame*>(a))
#define F_TO_C(a) (reinterpret_cast<audioframe*>(a))

struct audioproc *audioproc_create(){
	audioproc *a = TO_C(webrtc::AudioProcessing::Create());
	return a;
}

struct audioframe *audioframe_create(int channels, int sample_rate, int samples_per_block){
	webrtc::AudioFrame frame;
	frame.num_channels_ = channels;
	frame.sample_rate_hz_ = sample_rate;
	frame.samples_per_channel_ = samples_per_block;

	audioframe *a = F_TO_C(&frame);
	return a;
}

void audioframe_setdata(struct audioframe *frame, int16_t *block, size_t length){
	size_t i;
	webrtc::AudioFrame *af = F_TO_CPP(frame);
	for (i=0; i<length; i++)
		af->data_[i] = block[i];
}

void audioframe_getdata(struct audioframe *frame, int16_t *block, size_t length){
	size_t i;
	webrtc::AudioFrame *af = F_TO_CPP(frame);
	for (i=0; i<length; i++)
		block[i] = af->data_[i];
}

void audioproc_destroy(struct audioproc *apm){
	delete TO_CPP(apm);
}

void audioproc_hpf_en(struct audioproc *apm, int enable){
	TO_CPP(apm)->high_pass_filter()->Enable(enable);
}

void audioproc_aec_drift_comp_en(struct audioproc *apm, int enable){
	TO_CPP(apm)->echo_cancellation()->enable_drift_compensation(enable);
}

void audioproc_aec_en(struct audioproc *apm, int enable){
	TO_CPP(apm)->echo_cancellation()->Enable(enable);
}

void audioproc_aec_set_delay(struct audioproc *apm, int delay){
	TO_CPP(apm)->set_stream_delay_ms(delay);
}

void audioproc_aec_echo_ref(struct audioproc *apm, struct audioframe *frame){
	TO_CPP(apm)->AnalyzeReverseStream(F_TO_CPP(frame));
}

void audioproc_ns_set_level(struct audioproc *apm, int level){
	switch(level){
	case 0:
		TO_CPP(apm)->noise_suppression()->set_level(webrtc::NoiseSuppression::Level::kLow);
		break;
	case 1:
		TO_CPP(apm)->noise_suppression()->set_level(webrtc::NoiseSuppression::Level::kModerate);
		break;
	case 2:
		TO_CPP(apm)->noise_suppression()->set_level(webrtc::NoiseSuppression::Level::kHigh);
		break;
	case 3:
		TO_CPP(apm)->noise_suppression()->set_level(webrtc::NoiseSuppression::Level::kVeryHigh);
	}
}

void audioproc_ns_en(struct audioproc *apm, int enable){
	TO_CPP(apm)->noise_suppression()->Enable(enable);
}

void audioproc_agc_set_level_limits(struct audioproc *apm, int low, int high){
	TO_CPP(apm)->gain_control()->set_analog_level_limits(low, high);
}

void audioproc_agc_set_mode(struct audioproc *apm, int mode){
	switch(mode){
	case 0:
		TO_CPP(apm)->gain_control()->set_mode(webrtc::GainControl::Mode::kAdaptiveAnalog);
		break;
	case 1:
		TO_CPP(apm)->gain_control()->set_mode(webrtc::GainControl::Mode::kAdaptiveDigital);
		break;
	case 2:
		TO_CPP(apm)->gain_control()->set_mode(webrtc::GainControl::Mode::kFixedDigital);
	}
}

void audioproc_agc_en(struct audioproc *apm, int enable){
	TO_CPP(apm)->gain_control()->Enable(enable);
}

void audioproc_voice_det_en(struct audioproc *apm, int enable){
	TO_CPP(apm)->voice_detection()->Enable(enable);
}

int audioproc_voice_has_voice(struct audioproc *apm){
	return TO_CPP(apm)->voice_detection()->stream_has_voice();
}

int audioproc_process(struct audioproc *apm, struct audioframe *frame){
	return TO_CPP(apm)->ProcessStream(F_TO_CPP(frame));
}

