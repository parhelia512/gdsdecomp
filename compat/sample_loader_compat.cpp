#include "sample_loader_compat.h"
#include "compat/resource_compat_binary.h"

#include "core/io/file_access.h"

struct IMA_ADPCM_State {
	int16_t step_index = 0;
	int32_t predictor = 0;
	int32_t last_nibble = -1;
};

Ref<AudioStreamWAV> SampleLoaderCompat::convert_adpcm_to_16bit(const Ref<AudioStreamWAV> &p_sample) {
	ERR_FAIL_COND_V_MSG(p_sample->get_format() != AudioStreamWAV::FORMAT_IMA_ADPCM, Ref<AudioStreamWAV>(), "Sample is not IMA ADPCM.");
	Ref<AudioStreamWAV> new_sample = memnew(AudioStreamWAV);

	new_sample->set_format(AudioStreamWAV::FORMAT_16_BITS);
	new_sample->set_loop_mode(p_sample->get_loop_mode());
	new_sample->set_loop_begin(p_sample->get_loop_begin());
	new_sample->set_loop_end(p_sample->get_loop_end());
	new_sample->set_mix_rate(p_sample->get_mix_rate());
	new_sample->set_stereo(p_sample->is_stereo());

	IMA_ADPCM_State p_ima_adpcm[2];
	int32_t final, final_r;
	int64_t p_offset = 0;
	int64_t p_increment = 1;
	auto data = p_sample->get_data();
	bool is_stereo = p_sample->is_stereo();
	int64_t p_amount = data.size() * (is_stereo ? 1 : 2); // number of samples for EACH channel, not total
	Vector<uint8_t> dest_data;
	dest_data.resize(p_amount * sizeof(int16_t) * (is_stereo ? 2 : 1)); // number of 16-bit samples * number of channels
	int16_t *dest = (int16_t *)dest_data.ptrw();
	while (p_amount) {
		p_amount--;
		int64_t pos = p_offset;

		while (pos > p_ima_adpcm[0].last_nibble) {
			static const int16_t _ima_adpcm_step_table[89] = {
				7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
				19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
				50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
				130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
				337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
				876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
				2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
				5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
				15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
			};

			static const int8_t _ima_adpcm_index_table[16] = {
				-1, -1, -1, -1, 2, 4, 6, 8,
				-1, -1, -1, -1, 2, 4, 6, 8
			};

			for (int i = 0; i < (is_stereo ? 2 : 1); i++) {
				int16_t nibble, diff, step;

				p_ima_adpcm[i].last_nibble++;
				const uint8_t *src_ptr = (const uint8_t *)data.ptr();
				// src_ptr += AudioStreamWAV::DATA_PAD;
				int source_index = (p_ima_adpcm[i].last_nibble >> 1) * (is_stereo ? 2 : 1) + i;
				uint8_t nbb = src_ptr[source_index];
				nibble = (p_ima_adpcm[i].last_nibble & 1) ? (nbb >> 4) : (nbb & 0xF);
				step = _ima_adpcm_step_table[p_ima_adpcm[i].step_index];

				p_ima_adpcm[i].step_index += _ima_adpcm_index_table[nibble];
				if (p_ima_adpcm[i].step_index < 0) {
					p_ima_adpcm[i].step_index = 0;
				}
				if (p_ima_adpcm[i].step_index > 88) {
					p_ima_adpcm[i].step_index = 88;
				}

				diff = step >> 3;
				if (nibble & 1) {
					diff += step >> 2;
				}
				if (nibble & 2) {
					diff += step >> 1;
				}
				if (nibble & 4) {
					diff += step;
				}
				if (nibble & 8) {
					diff = -diff;
				}

				p_ima_adpcm[i].predictor += diff;
				if (p_ima_adpcm[i].predictor < -0x8000) {
					p_ima_adpcm[i].predictor = -0x8000;
				} else if (p_ima_adpcm[i].predictor > 0x7FFF) {
					p_ima_adpcm[i].predictor = 0x7FFF;
				}

				//printf("%i - %i - pred %i\n",int(p_ima_adpcm[i].last_nibble),int(nibble),int(p_ima_adpcm[i].predictor));
			}
		}

		final = p_ima_adpcm[0].predictor;
		if (is_stereo) {
			final_r = p_ima_adpcm[1].predictor;
		}

		*dest = final;
		dest++;
		if (is_stereo) {
			*dest = final_r;
			dest++;
		}
		p_offset += p_increment;
	}
	new_sample->set_data(dest_data);
	return new_sample;
}

Ref<AudioStreamWAV> SampleLoaderCompat::load_wav(const String &p_path, Error *r_err) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	if (!r_err) {
		r_err = &err;
	}
	ERR_FAIL_COND_V_MSG(err != OK, Ref<AudioStreamWAV>(), "Cannot open file '" + p_path + "'.");

	ResourceFormatLoaderCompatBinary rlcb;
	ResourceInfo i_info = rlcb.get_resource_info(p_path, r_err);
	ERR_FAIL_COND_V_MSG(*r_err != OK, Ref<AudioStreamWAV>(), "Cannot open resource '" + p_path + "'.");
	if (i_info.ver_major == 4) {
		Ref<AudioStreamWAV> sample = ResourceLoader::load(p_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &err);
		ERR_FAIL_COND_V_MSG(*r_err != OK, Ref<AudioStreamWAV>(), "Cannot open resource '" + p_path + "'.");
		return sample;
	}

	// otherwise, we have a version 2.x sample
	auto res = rlcb.custom_load(p_path, ResourceInfo::LoadType::FAKE_LOAD, r_err);

	ERR_FAIL_COND_V_MSG(*r_err, Ref<AudioStreamWAV>(), "Cannot load resource '" + p_path + "'.");
	String name = res->get("resource/name");
	AudioStreamWAV::LoopMode loop_mode = (AudioStreamWAV::LoopMode) int(res->get("loop_format"));
	// int loop_begin, loop_end, mix_rate, data_bytes = 0;
	int loop_begin = res->get("loop_begin");
	int loop_end = res->get("loop_end");
	bool stereo = res->get("stereo");

	int mix_rate = res->get("mix_rate");
	if (!mix_rate) {
		mix_rate = 44100;
	}
	Vector<uint8_t> data;
	AudioStreamWAV::Format format;
	int data_bytes = 0;
	Dictionary dat = res->get("data");
	if (dat.is_empty()) {
		data = res->get("data");
		format = (AudioStreamWAV::Format) int(res->get("format"));
	} else {
		String fmt = dat.get("format", "");
		if (fmt == "ima_adpcm") {
			format = AudioStreamWAV::FORMAT_IMA_ADPCM;
		} else if (fmt == "pcm16") {
			format = AudioStreamWAV::FORMAT_16_BITS;
		} else if (fmt == "pcm8") {
			format = AudioStreamWAV::FORMAT_8_BITS;
		} else {
			format = (AudioStreamWAV::Format)(int)res->get("format");
			// if it doesn't exist on the root res, then it'll return the default of FORMAT_8_BITS anyway.
		}
		data = dat.get("data", Vector<uint8_t>());
		if (dat.has("stereo")) {
			stereo = dat["stereo"];
		}
		if (dat.has("length")) {
			data_bytes = dat["length"];
		}
	}
	if (data_bytes == 0) {
		data_bytes = data.size();
	}
	// else if (data_bytes != data.size()) {
	// 	 TODO: something?
	// 	 WARN_PRINT("Data size mismatch: " + itos(data_bytes) + " vs " + itos(data.size()));
	// }
	// create a new sample
	Ref<AudioStreamWAV> sample = memnew(AudioStreamWAV);
	sample->set_name(name);
	sample->set_data(data);
	sample->set_format(format);
	sample->set_loop_mode(loop_mode);
	sample->set_stereo(stereo);
	sample->set_loop_begin(loop_begin);
	sample->set_loop_end(loop_end);
	sample->set_mix_rate(mix_rate);
	return sample;
}
void SampleLoaderCompat::_bind_methods() {}
