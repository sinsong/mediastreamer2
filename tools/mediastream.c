/*
mediastreamer2 library - modular sound and video processing and streaming
Copyright (C) 2006  Simon MORLAT (simon.morlat@linphone.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/


#include <math.h>
#include "common.h"
#include "mediastreamer2/msequalizer.h"
#include "mediastreamer2/msvolume.h"

#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ortp/b64.h>


#ifdef HAVE_CONFIG_H
#include "mediastreamer-config.h"
#endif


#define MEDIASTREAM_MAX_ICE_CANDIDATES 3

static int cond=1;

typedef enum _RcAlgo{
	RCAlgoNone,
	RCAlgoSimple,
	RCAlgoAdvanced,
	RCAlgoInvalid
}RcAlgo;

typedef struct _MediastreamIceCandidate {
	char ip[64];
	char type[6];
	int port;
} MediastreamIceCandidate;

typedef struct _MediastreamDatas {
	MSFactory *factory;
	int localport,remoteport,payload; // 本地 & 远程端口
	char ip[64]; // 远程 ip
	char *send_fmtp;
	char *recv_fmtp;
	int jitter;
	int bitrate;
	int mtu;
	MSVideoSize vs;
	RcAlgo rc_algo;
	bool_t ec;
	bool_t agc;
	bool_t eq;
	bool_t is_verbose;
	int device_rotation;
	VideoStream *video;
	char * capture_card;
	char * playback_card;
	char * camera;
	char *infile,*outfile;
	float ng_threshold;
	bool_t use_ng;
	bool_t two_windows;
	bool_t el;
	bool_t enable_srtp;
	
	bool_t interactive;
	bool_t enable_avpf;
	bool_t enable_rtcp;
	bool_t freeze_on_error;
	
	float el_speed;
	float el_thres;
	float el_force;
	int el_sustain;
	float el_transmit_thres;
	float ng_floorgain;
	bool_t enable_zrtp;
	PayloadType *custom_pt;
	int video_window_id;
	int preview_window_id;
	/* starting values echo canceller */
	int ec_len_ms, ec_delay_ms, ec_framesize;
	char* srtp_local_master_key;
	char* srtp_remote_master_key;
	OrtpNetworkSimulatorParams netsim;
	float zoom;
	float zoom_cx, zoom_cy;

	AudioStream *audio;
	PayloadType *pt;
	RtpSession *session; // RTP 会话
	OrtpEvQueue *q;
	RtpProfile *profile;
	MSBandwidthController *bw_controller;

	IceSession *ice_session;
	MediastreamIceCandidate ice_local_candidates[MEDIASTREAM_MAX_ICE_CANDIDATES];
	MediastreamIceCandidate ice_remote_candidates[MEDIASTREAM_MAX_ICE_CANDIDATES];
	int ice_local_candidates_nb;
	int ice_remote_candidates_nb;
	char * video_display_filter;
	FILE * logfile;
	bool_t enable_speaker;
} MediastreamDatas;
// 这就是参数


// MAIN METHODS
/* 初始化默认参数 */
MediastreamDatas* init_default_args(void);
/* 解析参数 */
bool_t parse_args(int argc, char** argv, MediastreamDatas* out);
/* 设置流 */
void setup_media_streams(MediastreamDatas* args);
/* 运行循环 */
void mediastream_run_loop(MediastreamDatas* args);
/* 结束 */
void clear_mediastreams(MediastreamDatas* args);

// 辅助方法
void stop_handler(int signum);
static bool_t parse_addr(const char *addr, char *ip, size_t len, int *port);
static bool_t parse_ice_addr(char* addr, char* type, size_t type_len, char* ip, size_t ip_len, int* port);
static void display_items(void *user_data, uint32_t csrc, rtcp_sdes_type_t t, const char *content, uint8_t content_len);
static void parse_rtcp(mblk_t *m);
static void parse_events(RtpSession *session, OrtpEvQueue *q);
static bool_t parse_window_ids(const char *ids, int* video_id, int* preview_id);
static RcAlgo parse_rc_algo(const char *algo);

const char *usage="mediastream --local <port>\n"
								"--remote <ip:port> \n"
								"[--help (display this help) ]\n"
								"[--payload <payload type number or payload name like 'audio/pmcu/8000'> ]\n"
								"[ --agc (enable automatic gain control) ]\n"
								"[ --bitrate <bits per seconds> ]\n"
								"[ --camera <camera id as listed at startup> ]\n"
								"[ --capture-card <name> ]\n"
								"[ --ec (enable echo canceller) ]\n"
								"[ --ec-delay <echo canceller delay in ms> ]\n"
								"[ --ec-framesize <echo canceller framesize in samples> ]\n"
								"[ --ec-tail <echo canceller tail length in ms> ]\n"
								"[ --el (enable echo limiter) ]\n"
								"[ --el-force <(float) [0-1]> (The proportional coefficient controlling the mic attenuation) ]\n"
								"[ --el-speed <(float) [0-1]> (gain changes are smoothed with a coefficent) ]\n"
								"[ --el-sustain <(int)> (Time in milliseconds for which the attenuation is kept unchanged after) ]\n"
								"[ --el-thres <(float) [0-1]> (Threshold above which the system becomes active) ]\n"
								"[ --el-transmit-thres <(float) [0-1]> (TO BE DOCUMENTED) ]\n"
								"[ --fmtp <fmtpline> ]\n"
								"[ --recv_fmtp <fmtpline passed to decoder> ]\n"
								"[ --freeze-on-error (for video, stop upon decoding error until next valid frame) ]\n"
								"[ --height <pixels> ]\n"
								"[ --ice-local-candidate <ip:port:[host|srflx|prflx|relay]> ]\n"
								"[ --ice-remote-candidate <ip:port:[host|srflx|prflx|relay]> ]\n"
								"[ --infile <input wav file> specify a wav file to be used for input, instead of soundcard ]\n"
								"[ --interactive (run in interactive mode) ]\n"
								"[ --jitter <miliseconds> ]\n"
								"[ --log <file> ]\n"
								"[ --mtu <mtu> (specify MTU)]\n"
								"[ --netsim-bandwidth <bandwidth limit in bits/s> (simulates a network download bandwidth limit) ]\n"
								"[ --netsim-consecutive-loss-probability <0-1> (to simulate bursts of lost packets) ]\n"
								"[ --netsim-jitter-burst-density <0-10> (density of gap/burst events, 1.0=one gap/burst per second in average) ]\n"
								"[ --netsim-jitter-strength <0-100> (strength of the jitter simulation) ]\n"
								"[ --netsim-latency <latency in ms> (simulates a network latency) ]\n"
								"[ --netsim-lossrate <0-100> (simulates a network lost rate) ]\n"
								"[ --netsim-mode inbound|outboud (whether network simulation is applied to incoming (default) or outgoing stream) ]\n"
								"[ --ng (enable noise gate)] \n"
								"[ --ng-floorgain <(float) [0-1]> (gain applied to the signal when its energy is below the threshold.) ]\n"
								"[ --ng-threshold <(float) [0-1]> (noise gate threshold) ]\n"
								"[ --no-avpf ]\n"
								"[ --no-rtcp ]\n"
								"[ --outfile <output wav file> specify a wav file to write audio into, instead of soundcard ]\n"
								"[ --playback-card <name> ]\n"
								"[ --rc <rate control algorithm> possible values are: none, simple, advanced ]\n"
								"[ --srtp <local master_key> <remote master_key> (enable srtp, master key is generated if absent from comand line) ]\n"
								"[ --verbose (most verbose messages) ]\n"
								"[ --video-display-filter <name> ]\n"
								"[ --video-windows-id <video surface:preview surface >]\n"
								"[ --width <pixels> ]\n"
								"[ --zoom zoom factor ]\n"
								"[ --zrtp (enable zrtp) ]\n"
								;




// 我们要看的 main 函数，入口点在这里！！！
int main(int argc, char * argv[])
{
	MediastreamDatas* args; //参数
	cond = 1;

	args = init_default_args(); //初始化默认参数

	// 从命令行解析参数
	if (!parse_args(argc, argv, args)){
		printf("%s",usage);
		return 0;
	}

	// 设置媒体流
	setup_media_streams(args);

	// 开始媒体流循环
	mediastream_run_loop(args);

	// 清理媒体流
	clear_mediastreams(args);

	free(args);

	return 0;
}


// 初始化默认参数
MediastreamDatas* init_default_args(void) {
	MediastreamDatas* args = (MediastreamDatas*)ms_malloc0(sizeof(MediastreamDatas));
	args->localport=0;
	args->remoteport=0;
	args->payload=0;
	memset(args->ip, 0, sizeof(args->ip));
	args->send_fmtp=NULL;
	args->recv_fmtp=NULL;
	args->jitter=50;
	args->bitrate=0;
	args->ec=FALSE;
	args->agc=FALSE;
	args->eq=FALSE;
	args->interactive=FALSE;
	args->is_verbose=FALSE;
	args->device_rotation=-1;
	args->rc_algo = RCAlgoNone;

#ifdef VIDEO_ENABLED
	args->video=NULL;
#endif
	args->capture_card=NULL;
	args->playback_card=NULL;
	args->camera=NULL;
	args->infile=args->outfile=NULL;
	args->ng_threshold=-1;
	args->use_ng=FALSE;
	args->two_windows=FALSE;
	args->el=FALSE;
	args->el_speed=-1;
	args->el_thres=-1;
	args->el_force=-1;
	args->el_sustain=-1;
	args->el_transmit_thres=-1;
	args->ng_floorgain=-1;
	args->enable_zrtp =FALSE;
	args->custom_pt=NULL;
	args->video_window_id = -1;
	args->preview_window_id = -1;
	args->enable_avpf = TRUE;
	args->enable_rtcp = TRUE;
	/* starting values echo canceller */
	args->ec_len_ms=args->ec_delay_ms=args->ec_framesize=0;
	args->enable_srtp = FALSE;
	args->srtp_local_master_key = args->srtp_remote_master_key = NULL;
	args->zoom = 1.0;
	args->zoom_cx = args->zoom_cy = 0.5;

	args->audio = NULL;
	args->session = NULL;
	args->pt = NULL;
	args->q = NULL;
	args->profile = NULL;
	args->logfile = NULL;

	args->ice_session = NULL;
	memset(args->ice_local_candidates, 0, sizeof(args->ice_local_candidates));
	memset(args->ice_remote_candidates, 0, sizeof(args->ice_remote_candidates));
	args->ice_local_candidates_nb = args->ice_remote_candidates_nb = 0;
	args->video_display_filter=NULL;

	return args;
}

bool_t parse_args(int argc, char** argv, MediastreamDatas* out) {
	int i;

	if (argc<4) {
		ms_error("Expected at least 3 arguments.\n");
		return FALSE;
	}

	/* default size */
	out->vs.width=MS_VIDEO_SIZE_CIF_W;
	out->vs.height=MS_VIDEO_SIZE_CIF_H;

	for (i=1;i<argc;i++){
		if (strcmp(argv[i],"--help")==0 || strcmp(argv[i],"-h")==0) {
			return FALSE;
		}else if (strcmp(argv[i],"--local")==0){ // 本地端口
			char *is_invalid;
			i++;
			out->localport = strtol(argv[i],&is_invalid,10);
			if (*is_invalid!='\0'){
				ms_error("Failed to parse local port '%s'\n",argv[i]);
				return 0;
			}
		}else if (strcmp(argv[i],"--remote")==0){ //远程地址
			i++;
			if (!parse_addr(argv[i],out->ip,sizeof(out->ip),&out->remoteport)) {
				ms_error("Failed to parse remote address '%s'\n",argv[i]);
				return FALSE;
			}
			ms_message("Remote addr: ip=%s port=%i\n",out->ip,out->remoteport);
		}else if (strcmp(argv[i],"--ice-local-candidate")==0) {
			MediastreamIceCandidate *candidate;
			i++;
			if (out->ice_local_candidates_nb>=MEDIASTREAM_MAX_ICE_CANDIDATES) {
				ms_warning("Ignore ICE local candidate \"%s\" (maximum %d candidates allowed)\n",argv[i],MEDIASTREAM_MAX_ICE_CANDIDATES);
				continue;
			}
			candidate=&out->ice_local_candidates[out->ice_local_candidates_nb];
			if (!parse_ice_addr(argv[i],candidate->type,sizeof(candidate->type),candidate->ip,sizeof(candidate->ip),&candidate->port)) {
				ms_error("Failed to parse ICE local candidates '%s'\n", argv[i]);
				return FALSE;
			}
			out->ice_local_candidates_nb++;
			ms_message("ICE local candidate: type=%s ip=%s port=%i\n",candidate->type,candidate->ip,candidate->port);
		}else if (strcmp(argv[i],"--ice-remote-candidate")==0) {
			MediastreamIceCandidate *candidate;
			i++;
			if (out->ice_remote_candidates_nb>=MEDIASTREAM_MAX_ICE_CANDIDATES) {
				ms_warning("Ignore ICE remote candidate \"%s\" (maximum %d candidates allowed)\n",argv[i],MEDIASTREAM_MAX_ICE_CANDIDATES);
				continue;
			}
			candidate=&out->ice_remote_candidates[out->ice_remote_candidates_nb];
			if (!parse_ice_addr(argv[i],candidate->type,sizeof(candidate->type),candidate->ip,sizeof(candidate->ip),&candidate->port)) {
				ms_error("Failed to parse ICE remote candidates '%s'\n", argv[i]);
				return FALSE;
			}
			out->ice_remote_candidates_nb++;
			ms_message("ICE remote candidate: type=%s ip=%s port=%i\n",candidate->type,candidate->ip,candidate->port);
		}else if (strcmp(argv[i],"--payload")==0){
			i++;
			if (isdigit(argv[i][0])){
				out->payload=atoi(argv[i]);
			}else {
				out->payload=114;
				out->custom_pt=ms_tools_parse_custom_payload(argv[i]);
			}
		}else if (strcmp(argv[i],"--fmtp")==0){
			i++;
			out->send_fmtp=argv[i];
		}else if (strcmp(argv[i],"--recv_fmtp")==0){
			i++;
			out->recv_fmtp=argv[i];
		}else if (strcmp(argv[i],"--jitter")==0){
			i++;
			out->jitter=atoi(argv[i]);
		}else if (strcmp(argv[i],"--bitrate")==0){
			i++;
			out->bitrate=atoi(argv[i]);
		}else if (strcmp(argv[i],"--width")==0){
			i++;
			out->vs.width=atoi(argv[i]);
		}else if (strcmp(argv[i],"--height")==0){
			i++;
			out->vs.height=atoi(argv[i]);
		}else if (strcmp(argv[i],"--capture-card")==0){
			i++;
			out->capture_card=argv[i];
		}else if (strcmp(argv[i],"--playback-card")==0){
			i++;
			out->playback_card=argv[i];
		}else if (strcmp(argv[i],"--ec")==0){
			out->ec=TRUE;
		}else if (strcmp(argv[i],"--ec-tail")==0){
			i++;
			out->ec_len_ms=atoi(argv[i]);
		}else if (strcmp(argv[i],"--ec-delay")==0){
			i++;
			out->ec_delay_ms=atoi(argv[i]);
		}else if (strcmp(argv[i],"--ec-framesize")==0){
			i++;
			out->ec_framesize=atoi(argv[i]);
		}else if (strcmp(argv[i],"--agc")==0){
			out->agc=TRUE;
		}else if (strcmp(argv[i],"--eq")==0){
			out->eq=TRUE;
		}else if (strcmp(argv[i],"--ng")==0){
			out->use_ng=1;
		}else if (strcmp(argv[i],"--rc")==0){
			i++;
			out->rc_algo = parse_rc_algo(argv[i]);
			if (out->rc_algo == RCAlgoInvalid){
				ms_error("Invalid argument for --rc");
				return FALSE;
			}
		}else if (strcmp(argv[i],"--ng-threshold")==0){
			i++;
			out->ng_threshold=(float)atof(argv[i]);
		}else if (strcmp(argv[i],"--ng-floorgain")==0){
			i++;
			out->ng_floorgain=(float)atof(argv[i]);
		}else if (strcmp(argv[i],"--two-windows")==0){
			out->two_windows=TRUE;
		}else if (strcmp(argv[i],"--infile")==0){
			i++;
			out->infile=argv[i];
		}else if (strcmp(argv[i],"--outfile")==0){
			i++;
			out->outfile=argv[i];
		}else if (strcmp(argv[i],"--camera")==0){
			i++;
			out->camera=argv[i];
		}else if (strcmp(argv[i],"--el")==0){
			out->el=TRUE;
		}else if (strcmp(argv[i],"--el-speed")==0){
			i++;
			out->el_speed=(float)atof(argv[i]);
		}else if (strcmp(argv[i],"--el-thres")==0){
			i++;
			out->el_thres=(float)atof(argv[i]);
		}else if (strcmp(argv[i],"--el-force")==0){
			i++;
			out->el_force=(float)atof(argv[i]);
		}else if (strcmp(argv[i],"--el-sustain")==0){
			i++;
			out->el_sustain=atoi(argv[i]);
		}else if (strcmp(argv[i],"--el-transmit-thres")==0){
			i++;
			out->el_transmit_thres=(float)atof(argv[i]);
		} else if (strcmp(argv[i],"--zrtp")==0){
			out->enable_zrtp = TRUE;
		} else if (strcmp(argv[i],"--verbose")==0){
			out->is_verbose=TRUE;
		} else if (strcmp(argv[i], "--video-windows-id")==0) {
			i++;
			if (!parse_window_ids(argv[i],&out->video_window_id, &out->preview_window_id)) {
				ms_error("Failed to parse window ids '%s'\n",argv[i]);
				return FALSE;
			}
		} else if (strcmp(argv[i], "--device-rotation")==0) {
			i++;
			out->device_rotation=atoi(argv[i]);
		} else if (strcmp(argv[i], "--srtp")==0) {
			if (!ms_srtp_supported()) {
				ms_error("srtp support not enabled");
				return FALSE;
			}
			out->enable_srtp = TRUE;
			i++;
			// check if we're being given keys
			if (i + 1 < argc) {
				out->srtp_local_master_key = argv[i++];
				out->srtp_remote_master_key = argv[i++];
			}
		} else if (strcmp(argv[i],"--netsim-bandwidth")==0){
			i++;
			if (i<argc){
				out->netsim.max_bandwidth=(float)atoi(argv[i]);
				out->netsim.enabled=TRUE;
			}else{
				ms_error("Missing argument for --netsim-bandwidth");
				return FALSE;
			}
		}else if (strcmp(argv[i],"--netsim-lossrate")==0){
			i++;
			if (i<argc){
				out->netsim.loss_rate=(float)atoi(argv[i]);
				if (out->netsim.loss_rate < 0 || out->netsim.loss_rate>100) {
					ms_error("Loss rate must be between 0 and 100.");
					return FALSE;
				}
				out->netsim.enabled=TRUE;
			}else{
				ms_error("Missing argument for --netsim-lossrate");
				return FALSE;
			}
		}else if (strcmp(argv[i],"--netsim-consecutive-loss-probability")==0){
			i++;
			if (i<argc){
				sscanf(argv[i],"%f",&out->netsim.consecutive_loss_probability);
				if (out->netsim.consecutive_loss_probability < 0 || out->netsim.consecutive_loss_probability>1) {
					ms_error("The consecutive loss probability must be between 0 and 1.");
					return FALSE;
				}

				out->netsim.enabled=TRUE;
			}else{
				ms_error("Missing argument for --netsim-consecutive-loss-probability");
				return FALSE;
			}
		}else if (strcmp(argv[i], "--netsim-latency") == 0) {
			i++;
			if (i<argc){
				out->netsim.latency = atoi(argv[i]);
				out->netsim.enabled=TRUE;
			}else{
				ms_error("Missing argument for --netsim-latency");
				return FALSE;
			}
		}else if (strcmp(argv[i], "--netsim-jitter-burst-density") == 0) {
			i++;
			if (i<argc){
				sscanf(argv[i],"%f",&out->netsim.jitter_burst_density);
				if (out->netsim.jitter_burst_density<0 || out->netsim.jitter_burst_density>10){
					ms_error("The jitter burst density must be between 0 and 10");
					return FALSE;
				}
				out->netsim.enabled=TRUE;
			}else{
				ms_error("Missing argument for --netsim-jitter-burst-density");
				return FALSE;
			}
		}else if (strcmp(argv[i], "--netsim-jitter-strength") == 0) {
			i++;
			if (i<argc){
				sscanf(argv[i],"%f",&out->netsim.jitter_strength);
				if (out->netsim.jitter_strength<0 || out->netsim.jitter_strength>100){
					ms_error("The jitter strength must be between 0 and 100.");
					return FALSE;
				}
				out->netsim.enabled=TRUE;
			}else{
				ms_error("Missing argument for --netsim-jitter-strength");
				return FALSE;
			}
		}else if (strcmp(argv[i], "--netsim-mode") == 0) {
			i++;
			if (i<argc){
				if (strcmp(argv[i],"inbound")==0)
					out->netsim.mode=OrtpNetworkSimulatorInbound;
				else if (strcmp(argv[i],"outbound")==0){
					out->netsim.mode=OrtpNetworkSimulatorOutbound;
				}else{
					ms_error("Invalid value for --netsim-mode");
					return FALSE;
				}
				out->netsim.enabled=TRUE;
			}else{
				ms_error("Missing argument for --netsim-dir");
				return FALSE;
			}
		}else if (strcmp(argv[i],"--zoom")==0){
			i++;
			if (sscanf(argv[i], "%f,%f,%f", &out->zoom, &out->zoom_cx, &out->zoom_cy) != 3) {
				ms_error("Invalid zoom triplet");
				return FALSE;
			}
		} else if (strcmp(argv[i],"--mtu")==0){
			i++;
			if (sscanf(argv[i], "%i", &out->mtu) != 1) {
				ms_error("Invalid mtu value");
				return FALSE;
			}
		} else if (strcmp(argv[i],"--interactive")==0){
			out->interactive=TRUE;
		} else if (strcmp(argv[i], "--no-avpf") == 0) {
			out->enable_avpf = FALSE;
		} else if (strcmp(argv[i], "--no-rtcp") == 0) {
			out->enable_rtcp = FALSE;
		} else if (strcmp(argv[i],"--help")==0) {
			return FALSE;
		} else if (strcmp(argv[i],"--video-display-filter")==0) {
			i++;
			out->video_display_filter=argv[i];
		} else if (strcmp(argv[i], "--log") == 0) {
			i++;
			out->logfile = fopen(argv[i], "a+");
		} else if (strcmp(argv[i], "--freeze-on-error") == 0) {
			out->freeze_on_error=TRUE;
		} else if (strcmp(argv[i], "--speaker") == 0) {
			out->enable_speaker=TRUE;
		} else {
			ms_error("Unknown option '%s'\n", argv[i]);
			return FALSE;
		}
	}
	if (out->netsim.jitter_burst_density>0 && out->netsim.max_bandwidth==0){
		ms_error("Jitter probability settings requires --netsim-bandwidth to be set.");
		return FALSE;
	}
	return TRUE;
}

static MSSndCard *get_sound_card(MSSndCardManager *manager, const char* card_name) {
	MSSndCard *play = ms_snd_card_manager_get_card(manager,card_name);
	if (play == NULL) {
		const MSList *list = ms_snd_card_manager_get_list(manager);
		char * cards = ms_strdup("");
		while (list) {
			MSSndCard *card = (MSSndCard*)list->data;
			cards = ms_strcat_printf(cards, "- %s\n", ms_snd_card_get_string_id(card));
			list = list->next;
		}
		ms_fatal("Specified card '%s' but could not find it. Available cards are:\n%s", card_name, cards);
		ms_free(cards);
	}
	return play;
}

// 设置媒体流
void setup_media_streams(MediastreamDatas* args) {
	/*create the rtp session */
#ifdef VIDEO_ENABLED
	MSWebCam *cam=NULL;
#endif
	MSFactory *factory;
	ortp_init();
	if (args->logfile)
		ortp_set_log_file(args->logfile);

	if (args->is_verbose) {
		ortp_set_log_level_mask(ORTP_LOG_DOMAIN, ORTP_DEBUG|ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR|ORTP_FATAL);
	} else {
		ortp_set_log_level_mask(ORTP_LOG_DOMAIN, ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR|ORTP_FATAL);
	}

	args->factory = factory = ms_factory_new_with_voip();

	rtp_profile_set_payload(&av_profile,110,&payload_type_speex_nb);
	rtp_profile_set_payload(&av_profile,111,&payload_type_speex_wb);
	rtp_profile_set_payload(&av_profile,112,&payload_type_ilbc);
	rtp_profile_set_payload(&av_profile,113,&payload_type_amr);
	rtp_profile_set_payload(&av_profile,114,args->custom_pt);
	rtp_profile_set_payload(&av_profile,115,&payload_type_lpc1015);

	args->profile=rtp_profile_clone_full(&av_profile);
	args->q=ortp_ev_queue_new();
	
	if (args->rc_algo == RCAlgoAdvanced){
		args->bw_controller = ms_bandwidth_controller_new();
	}

	if (args->mtu) ms_factory_set_mtu(factory, args->mtu);
	ms_factory_enable_statistics(factory, TRUE);
	ms_factory_reset_statistics(factory);

	args->ice_session=ice_session_new();
	ice_session_set_remote_credentials(args->ice_session,"1234","1234567890abcdef123456");
	// ICE local credentials are assigned when creating the ICE session, but force them here to simplify testing
	ice_session_set_local_credentials(args->ice_session,"1234","1234567890abcdef123456");
	ice_dump_session(args->ice_session);

	signal(SIGINT,stop_handler);
	args->pt=rtp_profile_get_payload(args->profile,args->payload);
	if (args->pt==NULL){
		ms_error("No payload defined with number %i.\n",args->payload);
		exit(-1);
	}
	if (args->enable_avpf == TRUE) {
		PayloadTypeAvpfParams avpf_params;
		payload_type_set_flag(args->pt, PAYLOAD_TYPE_RTCP_FEEDBACK_ENABLED);
		avpf_params.features = PAYLOAD_TYPE_AVPF_FIR | PAYLOAD_TYPE_AVPF_PLI | PAYLOAD_TYPE_AVPF_SLI | PAYLOAD_TYPE_AVPF_RPSI;
		avpf_params.trr_interval = 3000;
		payload_type_set_avpf_params(args->pt, avpf_params);
	} else {
		payload_type_unset_flag(args->pt, PAYLOAD_TYPE_RTCP_FEEDBACK_ENABLED);
	}
	if (args->send_fmtp!=NULL) payload_type_set_send_fmtp(args->pt,args->send_fmtp);
	if (args->recv_fmtp!=NULL) payload_type_set_recv_fmtp(args->pt,args->recv_fmtp);
	if (args->bitrate>0) args->pt->normal_bitrate=args->bitrate;

	if (args->pt->normal_bitrate==0){
		ms_error("Default bitrate specified for codec %s/%i. "
			"Please specify a network bitrate with --bitrate option.\n",args->pt->mime_type,args->pt->clock_rate);
		exit(-1);
	}

	// do we need to generate srtp keys ?
	if (args->enable_srtp) {
		// default profile require key-length = 30 bytes
		//  -> input : 40 b64 encoded bytes
		if (!args->srtp_local_master_key) {
			char tmp[30];
			snprintf(tmp,sizeof(tmp),"%08x%08x%08x%08x",rand(),rand(),rand(),rand());
			args->srtp_local_master_key = (char*) malloc(41);
			b64_encode((const char*)tmp, 30, args->srtp_local_master_key, 40);
			args->srtp_local_master_key[40] = '\0';
			ms_message("Generated local srtp key: '%s'", args->srtp_local_master_key);
		}
		if (!args->srtp_remote_master_key) {
			char tmp[30];
			snprintf(tmp,sizeof(tmp),"%08x%08x%08x%08x",rand(),rand(),rand(),rand());
			args->srtp_remote_master_key = (char*) malloc(41);
			b64_encode((const char*)tmp, 30, args->srtp_remote_master_key, 40);
			args->srtp_remote_master_key[40] = '\0';
			ms_message("Generated remote srtp key: '%s'", args->srtp_remote_master_key);
		}
	}

	if (args->pt->type!=PAYLOAD_VIDEO){
		MSSndCardManager *manager=ms_factory_get_snd_card_manager(factory);
		MSSndCard *capt= args->capture_card==NULL ? ms_snd_card_manager_get_default_capture_card(manager) :
				get_sound_card(manager,args->capture_card);
		MSSndCard *play= args->playback_card==NULL ? ms_snd_card_manager_get_default_capture_card(manager) :
				get_sound_card(manager,args->playback_card);
		args->audio=audio_stream_new(factory, args->localport,args->localport+1,ms_is_ipv6(args->ip)); // 音频流地址设置！！！
		if (args->bw_controller){
			ms_bandwidth_controller_add_stream(args->bw_controller, (MediaStream*)args->audio);
		}
		audio_stream_enable_automatic_gain_control(args->audio,args->agc);
		audio_stream_enable_noise_gate(args->audio,args->use_ng);
		audio_stream_set_echo_canceller_params(args->audio,args->ec_len_ms,args->ec_delay_ms,args->ec_framesize);
		audio_stream_enable_echo_limiter(args->audio,args->el);
		audio_stream_enable_adaptive_bitrate_control(args->audio,args->rc_algo == RCAlgoSimple);
		if (capt)
			ms_snd_card_set_preferred_sample_rate(capt,rtp_profile_get_payload(args->profile, args->payload)->clock_rate);
		if (play)
			ms_snd_card_set_preferred_sample_rate(play,rtp_profile_get_payload(args->profile, args->payload)->clock_rate);
		ms_message("Starting audio stream.\n");

		audio_stream_start_full(args->audio,args->profile,args->ip,args->remoteport,args->ip,args->enable_rtcp?args->remoteport+1:-1, args->payload, args->jitter,args->infile,args->outfile,
								args->outfile==NULL ? play : NULL ,args->infile==NULL ? capt : NULL,args->infile!=NULL ? FALSE: args->ec);

		if (args->ice_local_candidates_nb || args->ice_remote_candidates_nb) {
			args->audio->ms.ice_check_list = ice_check_list_new();
			rtp_session_set_pktinfo(args->audio->ms.sessions.rtp_session,TRUE);
			ice_session_add_check_list(args->ice_session, args->audio->ms.ice_check_list, 0);
		}
		if (args->ice_local_candidates_nb) {
			MediastreamIceCandidate *candidate;
			int c;
			for (c=0;c<args->ice_local_candidates_nb;c++){
				candidate=&args->ice_local_candidates[c];
				ice_add_local_candidate(args->audio->ms.ice_check_list,candidate->type,AF_INET,candidate->ip,candidate->port,1,NULL);
				ice_add_local_candidate(args->audio->ms.ice_check_list,candidate->type,AF_INET,candidate->ip,candidate->port+1,2,NULL);
			}
		}
		if (args->ice_remote_candidates_nb) {
			char foundation[4];
			MediastreamIceCandidate *candidate;
			int c;
			for (c=0;c<args->ice_remote_candidates_nb;c++){
				candidate=&args->ice_remote_candidates[c];
				memset(foundation, '\0', sizeof(foundation));
				snprintf(foundation, sizeof(foundation) - 1, "%u", c + 1);
				ice_add_remote_candidate(args->audio->ms.ice_check_list,candidate->type,AF_INET,candidate->ip,candidate->port,1,0,foundation,FALSE);
				ice_add_remote_candidate(args->audio->ms.ice_check_list,candidate->type,AF_INET,candidate->ip,candidate->port+1,2,0,foundation,FALSE);
			}
		}

		if (args->audio) {
			if (args->el) {
				if (args->el_speed!=-1)
					ms_filter_call_method(args->audio->volsend,MS_VOLUME_SET_EA_SPEED,&args->el_speed);
				if (args->el_force!=-1)
					ms_filter_call_method(args->audio->volsend,MS_VOLUME_SET_EA_FORCE,&args->el_force);
				if (args->el_thres!=-1)
					ms_filter_call_method(args->audio->volsend,MS_VOLUME_SET_EA_THRESHOLD,&args->el_thres);
				if (args->el_sustain!=-1)
					ms_filter_call_method(args->audio->volsend,MS_VOLUME_SET_EA_SUSTAIN,&args->el_sustain);
				if (args->el_transmit_thres!=-1)
					ms_filter_call_method(args->audio->volsend,MS_VOLUME_SET_EA_TRANSMIT_THRESHOLD,&args->el_transmit_thres);

			}
			if (args->use_ng){
				if (args->ng_threshold!=-1) {
					ms_filter_call_method(args->audio->volsend,MS_VOLUME_SET_NOISE_GATE_THRESHOLD,&args->ng_threshold);
					ms_filter_call_method(args->audio->volrecv,MS_VOLUME_SET_NOISE_GATE_THRESHOLD,&args->ng_threshold);
				}
				if (args->ng_floorgain != -1) {
					ms_filter_call_method(args->audio->volsend,MS_VOLUME_SET_NOISE_GATE_FLOORGAIN,&args->ng_floorgain);
					ms_filter_call_method(args->audio->volrecv,MS_VOLUME_SET_NOISE_GATE_FLOORGAIN,&args->ng_floorgain);
				}
			}

			if (args->enable_zrtp) {
				MSZrtpParams params = {0};
				audio_stream_enable_zrtp(args->audio,&params);
			}


			args->session=args->audio->ms.sessions.rtp_session;
		}

		if (args->enable_srtp) {
			ms_message("SRTP enabled: %d",
				audio_stream_enable_srtp(
					args->audio,
					MS_AES_128_SHA1_80,
					args->srtp_local_master_key,
					args->srtp_remote_master_key));
		}


	}else{
		ms_error("Error: video support not compiled.\n");
	}
	ice_session_set_base_for_srflx_candidates(args->ice_session);
	ice_session_compute_candidates_foundations(args->ice_session);
	ice_session_choose_default_candidates(args->ice_session);
	ice_session_choose_default_remote_candidates(args->ice_session);
	ice_session_start_connectivity_checks(args->ice_session);

	if (args->netsim.enabled){
		rtp_session_enable_network_simulation(args->session,&args->netsim);
	}
}

// 迭代
static void mediastream_tool_iterate(MediastreamDatas* args) {
	MSG msg;
	Sleep(10);
	while (PeekMessage(&msg, NULL, 0, 0,1)){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	/*no interactive mode on windows*/
}

// 开始媒体流循环
void mediastream_run_loop(MediastreamDatas* args) {
	// 注册消息队列
	rtp_session_register_event_queue(args->session,args->q);

	while(cond)
	{
		int n;
		for(n=0;n<500 && cond;++n){ // 500 次

			// 迭代
			mediastream_tool_iterate(args);

			// 音频流迭代
			if (args->audio) audio_stream_iterate(args->audio);
		}
		// 显示 rtp 信息
		rtp_stats_display(rtp_session_get_stats(args->session),"RTP stats");
		if (args->session){
			float audio_load = 0;
			float video_load = 0;

			ms_message("Bandwidth usage: download=%f kbits/sec, upload=%f kbits/sec\n",
				rtp_session_get_recv_bandwidth(args->session)*1e-3,
				rtp_session_get_send_bandwidth(args->session)*1e-3);

			if (args->audio) {
				audio_load = ms_ticker_get_average_load(args->audio->ms.sessions.ticker);
			}

			ms_message("Thread processing load: audio=%f\tvideo=%f", audio_load, video_load);
			parse_events(args->session,args->q);
			ms_message("Quality indicator : %f\n",args->audio ? audio_stream_get_quality_rating(args->audio) : media_stream_get_quality_rating((MediaStream*)args->video));
		}
	}
}

void clear_mediastreams(MediastreamDatas* args) {
	ms_message("stopping all...\n");
	ms_message("Average quality indicator: %f",args->audio ? audio_stream_get_average_quality_rating(args->audio) : -1);
	
	if (args->bw_controller){
		ms_bandwidth_controller_destroy(args->bw_controller);
	}
	
	if (args->audio) {
		audio_stream_stop(args->audio);
	}

	if (args->ice_session) ice_session_destroy(args->ice_session);
	ortp_ev_queue_destroy(args->q);
	rtp_profile_destroy(args->profile);

	if (args->logfile)
		fclose(args->logfile);

	ms_factory_destroy(args->factory);
}

// HELPER METHODS
void stop_handler(int signum)
{
	cond--;
	if (cond<0) {
		ms_error("Brutal exit (%d)\n", cond);
		exit(-1);
	}
}

static bool_t parse_addr(const char *addr, char *ip, size_t len, int *port)
{
	const char *semicolon=NULL;
	size_t iplen;
	int slen;
	const char *p;

	*port=0;

	for (p=addr+strlen(addr)-1;p>addr;p--){
		if (*p==':') {
			semicolon=p;
			break;
		}
	}
	/*if no semicolon is present, we can assume that user provided only port*/
	if (semicolon==NULL) {
		const char *localhost = "127.0.0.1";
		char * end;
		*port = strtol(addr, &end, 10);
		if (*end != '\0' || end == addr) {
			return FALSE;
		}
		strncpy(ip,localhost, MIN(len, strlen(localhost)));
		return TRUE;
	}
	iplen=semicolon-addr;
	slen=MIN(iplen,len-1);
	strncpy(ip,addr,slen);
	ip[slen]='\0';
	*port=atoi(semicolon+1);
	return TRUE;
}

static bool_t parse_ice_addr(char *addr, char *type, size_t type_len, char *ip, size_t ip_len, int *port)
{
	char *semicolon=NULL;
	size_t slen;

	semicolon=strrchr(addr,':');
	if (semicolon==NULL) return FALSE;
	slen=MIN(strlen(semicolon+1),type_len);
	strncpy(type,semicolon+1,slen);
	type[slen]='\0';
	*semicolon='\0';
	return parse_addr(addr,ip,ip_len,port);
}

static void display_items(void *user_data, uint32_t csrc, rtcp_sdes_type_t t, const char *content, uint8_t content_len){
	char str[256];
	int len=MIN(sizeof(str)-1,content_len);
	strncpy(str,content,len);
	str[len]='\0';
	switch(t){
		case RTCP_SDES_CNAME:
			ms_message("Found CNAME=%s",str);
		break;
		case RTCP_SDES_TOOL:
			ms_message("Found TOOL=%s",str);
		break;
		case RTCP_SDES_NOTE:
			ms_message("Found NOTE=%s",str);
		break;
		default:
			ms_message("Unhandled SDES item (%s)",str);
	}
}

static void parse_rtcp(mblk_t *m){
	do{
		if (rtcp_is_RR(m)){
			ms_message("Receiving RTCP RR");
		}else if (rtcp_is_SR(m)){
			ms_message("Receiving RTCP SR");
		}else if (rtcp_is_SDES(m)){
			ms_message("Receiving RTCP SDES");
			rtcp_sdes_parse(m,display_items,NULL);
		}else {
			ms_message("Receiving unhandled RTCP message");
		}
	}while(rtcp_next_packet(m));
}

static void parse_events(RtpSession *session, OrtpEvQueue *q){
	OrtpEvent *ev;

	while((ev=ortp_ev_queue_get(q))!=NULL){
		OrtpEventData *d=ortp_event_get_data(ev);
		switch(ortp_event_get_type(ev)){
			case ORTP_EVENT_RTCP_PACKET_RECEIVED:
				parse_rtcp(d->packet);
			break;
			case ORTP_EVENT_RTCP_PACKET_EMITTED:
				ms_message("Jitter buffer size: %f ms",rtp_session_get_jitter_stats(session)->jitter_buffer_size_ms);
			break;
			default:
			break;
		}
		ortp_event_destroy(ev);
	}
}

static bool_t parse_window_ids(const char *ids, int* video_id, int* preview_id)
{
	char* copy = strdup(ids);
	char *semicolon=strchr(copy,':');
	if (semicolon==NULL) {
		free(copy);
		return FALSE;
	}
	*semicolon = '\0';

	*video_id=atoi(copy);
	*preview_id=atoi(semicolon+1);
	free(copy);
	return TRUE;
}

static RcAlgo parse_rc_algo(const char *algo){
	if (algo == NULL) return RCAlgoInvalid;
	if (strcasecmp(algo,"simple")==0) return RCAlgoSimple;
	if (strcasecmp(algo, "advanced")==0) return RCAlgoAdvanced;
	if (strcasecmp(algo, "none")==0) return RCAlgoNone;
	return RCAlgoInvalid;
}
