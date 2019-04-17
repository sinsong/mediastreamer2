#include "mediastreamer2/mediastream.h"
#include "mediastreamer2/msequalizer.h"
#include "mediastreamer2/msvolume.h"

#include <math.h>
#include <signal.h>

#include <ortp/b64.h>

#ifdef HAVE_CONFIG_H
#include "mediastreamer-config.h"
#endif

typedef int bool;

// args
MSFactory *factory;
int localport;   // 本地端口
int remoteport;  // 远程端口
char ip[64];     // 远程ip
int payload;     // 暂时不明
int jitter;

char *capture_card;

AudioStream *audio; // 音频流
PayloadType *pt;
RtpSession *session; // RTP 会话
OrtpEvQueue *q; // 消息队列
RtpProfile *profile;
IceSession *ice_session;

bool enable_avpf;
bool enable_rtcp;
FILE *logfile;
static int cond=1;

void stop_handler(int signum)
{
	cond--;
	if (cond<0) {
		ms_error("Brutal exit (%d)\n", cond);
		exit(-1);
	}
}

static void display_items(void *user_data, uint32_t csrc, rtcp_sdes_type_t t, const char *content, uint8_t content_len)
{
	char str[256];
	int len=MIN(sizeof(str)-1,content_len);
	strncpy(str,content,len);
	str[len]='\0';
	switch(t)
	{
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
		if (rtcp_is_RR(m))
		{
			ms_message("Receiving RTCP RR");
		}else 
			if (rtcp_is_SR(m))
			{
			ms_message("Receiving RTCP SR");
			}
			else 
				if (rtcp_is_SDES(m))
				{
					ms_message("Receiving RTCP SDES");
					rtcp_sdes_parse(m,display_items,NULL);
				}
				else
				{
					ms_message("Receiving unhandled RTCP message");
				}
	}while(rtcp_next_packet(m));
}

static void parse_events(RtpSession *session, OrtpEvQueue *q){
	OrtpEvent *ev;

	while((ev=ortp_ev_queue_get(q))!=NULL)
	{
		OrtpEventData *d=ortp_event_get_data(ev);
		switch(ortp_event_get_type(ev))
		{
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

int main()
{
	// init--------------------------------------------------------------------------
	localport = 0;
	remoteport = 0;
	payload = 0;
	memset(ip, 0, sizeof(ip)); // {\0}
	audio = NULL;
	session = NULL;
	logfile = NULL;
	q = NULL;
	ice_session = NULL;
	pt = NULL;
	profile = NULL;
	enable_avpf = 1; //TRUE
	enable_rtcp = 1; //TRUE
	capture_card = NULL;
	jitter = 50;
	
	//args
	logfile = fopen("ortp.log", "a+");
	
	// setup-------------------------------------------------------------------------------
	ortp_init();
	// 如果想用日志
	ortp_set_log_file(logfile);
	
	factory = ms_factory_new_with_voip();
	// ??? 不检查吗？
	
	rtp_profile_set_payload(&av_profile,110,&payload_type_speex_nb);
	rtp_profile_set_payload(&av_profile,111,&payload_type_speex_wb);
	rtp_profile_set_payload(&av_profile,112,&payload_type_ilbc);
	rtp_profile_set_payload(&av_profile,113,&payload_type_amr);
	rtp_profile_set_payload(&av_profile,114,/*args->custom_pt = NULL*/ NULL);
	rtp_profile_set_payload(&av_profile,115,&payload_type_lpc1015);
	
	profile=rtp_profile_clone_full(&av_profile);
	q=ortp_ev_queue_new();
	ice_session=ice_session_new();
	ice_session_set_remote_credentials(ice_session,"1234","1234567890abcdef123456");
	// ICE local credentials are assigned when creating the ICE session, but force them here to simplify testing
	ice_session_set_local_credentials(ice_session,"1234","1234567890abcdef123456");
	ice_dump_session(ice_session);
	
	signal(SIGINT, stop_handler); // SIGINT 信号处理
	
	pt = rtp_profile_get_payload(profile, payload);
	if (pt == NULL)
	{
		fprintf(stderr, "No payload defined with number %i.\n", payload);
	}
	
	// avpf ???
	if (enable_avpf == TRUE) {
		PayloadTypeAvpfParams avpf_params;
		payload_type_set_flag(pt, PAYLOAD_TYPE_RTCP_FEEDBACK_ENABLED);
		avpf_params.features = PAYLOAD_TYPE_AVPF_FIR | PAYLOAD_TYPE_AVPF_PLI | PAYLOAD_TYPE_AVPF_SLI | PAYLOAD_TYPE_AVPF_RPSI;
		avpf_params.trr_interval = 3000;
		payload_type_set_avpf_params(pt, avpf_params);
	} else {
		payload_type_unset_flag(pt, PAYLOAD_TYPE_RTCP_FEEDBACK_ENABLED);
	}
	
	// send_fmtp 默认 NULL 不看了
	// recv_fmtp 也是
	
	if (pt->normal_bitrate==0){
		fprintf(stderr, "Default bitrate specified for codec %s/%i. "
			"Please specify a network bitrate with --bitrate option.\n", pt->mime_type, pt->clock_rate);
		exit(-1);
	}
	
	// enable_srtp 默认 FALSE 不看了
	
	// 估计就是音频
	
	MSSndCardManager *manager = ms_factory_get_snd_card_manager(factory);
	MSSndCard *capt = ms_snd_card_manager_get_default_capture_card(manager);
	MSSndCard *play = ms_snd_card_manager_get_default_capture_card(manager);
	audio = audio_stream_new(factory, localport, localport+1, ms_is_ipv6(ip));
	// bw_controller 不管 rc_algo 默认 RcNone
	audio_stream_enable_automatic_gain_control(audio, FALSE); // 第二个参数 agc 默认 FALSE
	audio_stream_enable_noise_gate(audio, FALSE); // use_ng 默认 FALSE
	audio_stream_set_echo_canceller_params(audio, 0, 0, 0); // 默认都是 0
	audio_stream_enable_echo_limiter(audio, FALSE); // el 默认值
	audio_stream_enable_adaptive_bitrate_control(audio, FALSE); //rc_algo 默认值的原因
	if (capt)
		ms_snd_card_set_preferred_sample_rate(capt,rtp_profile_get_payload(profile, payload)->clock_rate);
	if (play)
		ms_snd_card_set_preferred_sample_rate(play,rtp_profile_get_payload(profile, payload)->clock_rate);
	
	printf("Starting audio stream.\n");
	
	audio_stream_start_full(audio,
	                        profile,
							ip,
							remoteport,
							ip,
							enable_rtcp? remoteport+1:-1,
							payload,
							jitter,
							NULL, //infile 没有就NULL
							NULL, //outfile: char * 有->true 没有->false
							play, // 没有 outfile 就声卡，不然就NULL
							capt, // 同上，不过是infile
							0); //ec 默认 FALSE
	// candidates 不懂
	
	if(audio)
	{
		session = audio->ms.sessions.rtp_session;
	}
	
	ice_session_set_base_for_srflx_candidates(ice_session);
	ice_session_compute_candidates_foundations(ice_session);
	ice_session_choose_default_candidates(ice_session);
	ice_session_choose_default_remote_candidates(ice_session);
	ice_session_start_connectivity_checks(ice_session);

	// run_loop -------------------------------------------------------------------------
	// 注册消息队列
	rtp_session_register_event_queue(session,q);

	while(cond)
	{
		int n;
		for(n=0;n<500 && cond;++n){ // 500 次

			// 迭代
			//mediastream_tool_iterate(args); // 不想管它（笑）

			// 音频流迭代
			if (audio) audio_stream_iterate(audio);
		}
		
		// 显示 rtp 信息
		rtp_stats_display(rtp_session_get_stats(session),"RTP stats");
		if (session){
			float audio_load = 0;

			ms_message("Bandwidth usage: download=%f kbits/sec, upload=%f kbits/sec\n",
				rtp_session_get_recv_bandwidth(session)*1e-3,
				rtp_session_get_send_bandwidth(session)*1e-3);

			if (audio) {
				audio_load = ms_ticker_get_average_load(audio->ms.sessions.ticker);
			}

			ms_message("Thread processing load: audio=%f", audio_load);
			parse_events(session,q); // 这是个局部函数。。。
			ms_message("Quality indicator : %f\n", audio_stream_get_quality_rating(audio));
		}
	}
	
	// clear -------------------------------------------------------------------
	ms_message("stopping all...\n");
	ms_message("Average quality indicator: %f",audio ? audio_stream_get_average_quality_rating(audio) : -1);
	
	
	if (audio) {
		audio_stream_stop(audio);
	}
	
	if (ice_session) ice_session_destroy(ice_session);
	ortp_ev_queue_destroy(q);
	rtp_profile_destroy(profile);

	if (logfile)
		fclose(logfile);

	ms_factory_destroy(factory);
	
	// OK 没了
	
	return 0;
}