#ifndef WEBRTC_WRAPPER_H
#define WEBRTC_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif
	struct audioproc;
	struct audioframe;

	struct audioproc *audioproc_create();
	struct audioframe *audioframe_create(int channels, int sample_rate, int samples_per_block);
	void audioframe_setdata(struct audioframe *frame, int16_t *block, size_t length);
        void audioframe_getdata(struct audioframe *frame, int16_t *block, size_t length);

	void audioproc_destroy(struct audioproc *apm);

	void audioproc_hpf_en(struct audioproc *apm, int enable);

	void audioproc_aec_drift_comp_en(struct audioproc *apm, int enable);
	void audioproc_aec_en(struct audioproc *apm, int enable);
	void audioproc_aec_set_delay(struct audioproc *apm, int delay);
	void audioproc_aec_delayag_en(struct audioproc *apm);
	void audioproc_aec_echo_ref(struct audioproc *apm, struct audioframe *frame);

	void audioproc_ns_set_level(struct audioproc *apm, int level);
	void audioproc_ns_en(struct audioproc *apm, int enable);

	void audioproc_agc_set_level_limits(struct audioproc *apm, int low, int high);
	void audioproc_agc_set_mode(struct audioproc *apm, int mode);
	void audioproc_agc_en(struct audioproc *apm, int enable);

	void audioproc_voice_det_en(struct audioproc *apm, int enable);
	int audioproc_voice_has_voice(struct audioproc *apm);

	int audioproc_process(struct audioproc *apm, struct audioframe *frame);
#ifdef __cplusplus
}
#endif

#endif

