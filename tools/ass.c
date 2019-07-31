// Audio Stream Specialization

#include "mediastreamer2/mediastream.h"
#include "mediastreamer2/dtmfgen.h"
#include "mediastreamer2/mssndcard.h"
#include "mediastreamer2/msrtp.h"
#include "mediastreamer2/msfileplayer.h"
#include "mediastreamer2/msfilerec.h"
#include "mediastreamer2/msvolume.h"
#include "mediastreamer2/msequalizer.h"
#include "mediastreamer2/mstee.h"
#include "mediastreamer2/msaudiomixer.h"
#include "mediastreamer2/mscodecutils.h"
#include "mediastreamer2/msitc.h"
#include "mediastreamer2/msvaddtx.h"
#include "mediastreamer2/msgenericplc.h"
#include "mediastreamer2/mseventqueue.h"

#include <math.h>
#include <signal.h>
#include <sys/socket.h>

#include <ortp/b64.h>

#include <stdio.h>
#include <string.h>

MSFactory *factory;
RtpSession *session;
PayloadType *pt;
OrtpEvQueue *q;
RtpProfile *profile;

int lport;
// ip:rport
char ip[64];
int rport;

int payload;

int rate = 8000;

static void on_silence_detected(void *data, MSFilter *f, unsigned int event_id, void *event_arg);

typedef struct _AudioStreamSpecialization
{
    MSFilter *soundread;
    MSFilter *soundwrite;
    MSFilter *ec;
    MSFilter *vaddtx;
    MSFilter *rtprecv;
    MSFilter *rtpsend;
    MSFilter *encoder;
    MSFilter *decoder;
    MSFilter *mixer;

    MSTicker *ticker;

    RtpSession *rtp_session;

    // MediaStream
    MSFactory *factory;
    MSQualityIndicator *qi;
    //OrtpEvDispatcher *evd;
    OrtpEvQueue *evq;
} AudioStreamSpecialization;

AudioStreamSpecialization *audio_stream_specialization(MSFactory *factory, int lport, int lport_rtcp);
void audio_stream_specialization_free(AudioStreamSpecialization *ass);
int audio_stream_specialization_start(AudioStreamSpecialization *ass, RtpProfile *profile, const char *r_rtp_ip, int r_rtp_port, const char *r_rtcp_ip, int r_rtcp_port, int payload, MSSndCard *captcard, MSSndCard *playcard);
void audio_stream_specialization_stop(AudioStreamSpecialization *ass);

AudioStreamSpecialization *ass = NULL;

int main()
{
    lport = 20020;

    strcpy(ip, "127.0.0.1\n");
    rport = 20020;

    printf("[ass] local:  0.0.0.0:%d", lport);
    printf("[ass] remote: %s:%d", ip, rport);

    payload = 121;
    session = NULL;
    q = NULL;
    pt = NULL;
    profile = NULL;

    ortp_init();
    ortp_set_log_level_mask(ORTP_LOG_DOMAIN, ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR|ORTP_FATAL);
    
    factory = ms_factory_new_with_voip();

    // profile
    profile = rtp_profile_clone_full(&av_profile);
    rtp_profile_set_payload(profile, 121, &payload_type_opus);
    pt = rtp_profile_get_payload(profile, payload);

    // Sound Card
    MSSndCardManager *manager = ms_factory_get_snd_card_manager(factory);
	MSSndCard *capt = ms_snd_card_manager_get_default_capture_card(manager);
	MSSndCard *play = ms_snd_card_manager_get_default_capture_card(manager);

    // AudioStreamSpecialization Create!
    ass = audio_stream_specialization(factory, lport, lport + 1);

    if (capt)
		ms_snd_card_set_preferred_sample_rate(capt,rtp_profile_get_payload(profile, payload)->clock_rate);
	if (play)
		ms_snd_card_set_preferred_sample_rate(play,rtp_profile_get_payload(profile, payload)->clock_rate);

    audio_stream_specialization_start(ass, profile, ip, rport, ip, rport+1, payload, capt, play);

    // 开始
    puts("[ass] enter \'q\' to exit!");
    while(getchar() != 'q')
    {

    }

    if(ass)
    {
        audio_stream_specialization_stop(ass);
    }

    ms_factory_destroy(factory);

    return 0;
}

void audio_stream_specialization_free(AudioStreamSpecialization *ass)
{
    if(ass->rtp_session) rtp_session_unregister_event_queue(ass->rtp_session, ass->evq);
    if(ass->evq) ortp_ev_queue_destroy(ass->evq);
    if (ass->rtpsend) ms_filter_destroy(ass->rtpsend);
	if (ass->rtprecv) ms_filter_destroy(ass->rtprecv);
	if (ass->encoder) ms_filter_destroy(ass->encoder);
	if (ass->decoder) ms_filter_destroy(ass->decoder);
	if (ass->qi) ms_quality_indicator_destroy(ass->qi);

    if (ass->soundread) ms_filter_destroy(ass->soundread);
	if (ass->soundwrite) ms_filter_destroy(ass->soundwrite);
    if (ass->ec)	ms_filter_destroy(ass->ec);
    if (ass->vaddtx) ms_filter_destroy(ass->vaddtx);
    if(ass->mixer) ms_filter_destroy(ass->mixer);

    ms_free(ass); // memory free
}

AudioStreamSpecialization *audio_stream_specialization(MSFactory *factory, int lport, int lport_rtcp)
{
    AudioStreamSpecialization *ass;

    ass = (AudioStreamSpecialization *)ms_new0(AudioStreamSpecialization, 1);

    ass->rtp_session = rtp_session_new(RTP_SESSION_SENDRECV);
    rtp_session_set_local_addr(ass->rtp_session, "0.0.0.0", lport, lport_rtcp); // 本地地址
    //disable_checksums(rtp_session_get_rtp_socket(ass->rtp_session));

    //media_stream_init
    //ass->evd = 不要了
    ass->evq = ortp_ev_queue_new();
    ass->factory = factory;

    rtp_session_register_event_queue(ass->rtp_session, ass->evq);

    ms_factory_enable_statistics(factory, TRUE);
    ms_factory_reset_statistics(factory);

    rtp_session_resync(ass->rtp_session);

    ass->rtpsend = ms_factory_create_filter(factory, MS_RTP_SEND_ID);
    ass->qi = ms_quality_indicator_new(ass->rtp_session);
    ms_quality_indicator_set_label(ass->qi, "audio");
    // audio_stream_process_rtcp 不要了
    
    const char *echo_canceller_filtername = ms_factory_get_echo_canceller_filter_name(factory);
    MSFilterDesc *ec_desc = NULL;
    if(echo_canceller_filtername) ec_desc = ms_factory_lookup_filter_by_name(factory, echo_canceller_filtername);
    if(ec_desc) ass->ec = ms_factory_create_filter_from_desc(factory, ec_desc);
    
    return ass;
}

int audio_stream_specialization_start(AudioStreamSpecialization *ass, RtpProfile *profile, const char *r_rtp_ip, int r_rtp_port, const char *r_rtcp_ip, int r_rtcp_port, int payload, MSSndCard *captcard, MSSndCard *playcard)
{
    RtpSession *rtps = ass->rtp_session;
    PayloadType *pt;
    MSConnectionHelper h;

    rtp_session_set_profile(rtps, profile);

    // 设置端口
    rtp_session_set_remote_addr_full(rtps, r_rtcp_ip, r_rtp_port, r_rtcp_ip, r_rtcp_port);
    rtp_session_enable_rtcp(rtps, TRUE); // 如果rtcp_port是0就设置为FALSE

    rtp_session_set_payload_type(rtps, payload);

    ms_filter_call_method(ass->rtpsend, MS_RTP_SEND_SET_SESSION, rtps);
    ass->rtprecv = ms_factory_create_filter(ass->factory, MS_RTP_RECV_ID);
    ms_filter_call_method(ass->rtprecv, MS_RTP_RECV_SET_SESSION, rtps);
    ass->rtp_session = rtps; //可能是被修改了。。。

    ass->soundread = ms_snd_card_create_reader(captcard);
    ass->soundwrite = ms_snd_card_create_writer(playcard);

    pt = rtp_profile_get_payload(profile, payload);
    if(pt == NULL)
    {
        fprintf(stderr, "AudioStreamSpecialization: undefined payload type.\n");
        return -1;
    }

    // vaddtx
    ass->vaddtx = ms_factory_create_filter(ass->factory, MS_VAD_DTX_ID);
    if(ass->vaddtx)
    {
        ms_filter_add_notify_callback(ass->vaddtx, on_silence_detected, ass, TRUE);
    }

    ass->encoder = ms_factory_create_encoder(ass->factory, pt->mime_type);
    ass->decoder = ms_factory_create_decoder(ass->factory, pt->mime_type);

    ass->mixer = ms_factory_create_filter(ass->factory, MS_AUDIO_MIXER_ID);

    ass->ticker = ms_ticker_new();

    ms_filter_call_method(ass->soundread, MS_FILTER_SET_SAMPLE_RATE, &rate);
    ms_filter_call_method(ass->encoder, MS_FILTER_SET_SAMPLE_RATE, &rate);
    ms_filter_call_method(ass->decoder, MS_FILTER_SET_SAMPLE_RATE, &rate);
    ms_filter_call_method(ass->mixer, MS_FILTER_SET_SAMPLE_RATE, &rate);
    ms_filter_call_method(ass->rtprecv, MS_FILTER_SET_SAMPLE_RATE, &pt->clock_rate);

    // 发送图
    ms_connection_helper_start(&h);
    ms_connection_helper_link(&h, ass->soundread, -1, 0);
    if(ass->ec) ms_connection_helper_link(&h, ass->ec, 0, 0);
    if(ass->vaddtx) ms_connection_helper_link(&h, ass->vaddtx, 0, 0);
    ms_connection_helper_link(&h, ass->encoder, 0, 0);
    ms_connection_helper_link(&h, ass->rtpsend, 0, -1);

    // 接收图
    ms_connection_helper_start(&h);
    ms_connection_helper_link(&h, ass->rtprecv, -1, 0);
    ms_connection_helper_link(&h, ass->decoder, 0, 0);
    ms_connection_helper_link(&h, ass->mixer, 0, 0);
    ms_connection_helper_link(&h, ass->soundwrite, 0, -1);

    ms_ticker_attach_multiple(ass->ticker, ass->soundread, ass->rtprecv, NULL);

    return 0;
}

void audio_stream_specialization_stop(AudioStreamSpecialization *ass)
{
    if(ass->ticker)
    {
        ms_ticker_detach(ass->ticker, ass->soundread);
        ms_ticker_detach(ass->ticker, ass->rtprecv);

        MSConnectionHelper h;
        // 拆开发送图
		ms_connection_helper_start(&h);
		ms_connection_helper_unlink(&h,ass->soundread,-1,0);
		if (ass->ec)        ms_connection_helper_unlink(&h,ass->ec,1,1);
        if (ass->vaddtx)    ms_connection_helper_unlink(&h,ass->vaddtx, 0,0);
		if (ass->encoder)   ms_connection_helper_unlink(&h,ass->encoder,0,0);
		ms_connection_helper_unlink(&h,ass->rtpsend,0,-1);

        // 拆开接收图
		ms_connection_helper_start(&h);
		ms_connection_helper_unlink(&h,ass->rtprecv,-1,0);
		if (ass->decoder) ms_connection_helper_unlink(&h,ass->decoder,0,0);
		if (ass->mixer)   ms_connection_helper_unlink(&h, ass->mixer, 0, 0);
		ms_connection_helper_unlink(&h,ass->soundwrite,0,-1);
    }
}

static void on_silence_detected(void *data, MSFilter *f, unsigned int event_id, void *event_arg)
{
    AudioStreamSpecialization *ass = (AudioStreamSpecialization*)data;
    if(!ass->rtpsend)
        return;
    switch(event_id)
    {
    case MS_VAD_DTX_NO_VOICE:
        ms_message("vaddtx: no voice!")
        //ms_filter_call_method(ass->rtpsend, MS_RTP_SEND_SEND_GENERIC_CN, event_arg);
        ms_filter_call_method(ass->rtpsend, MS_RTP_SEND_MUTE, event_arg);
        break;
    case MS_VAD_DTX_VOICE:
        ms_message("vaddtx: have voice!")
        ms_filter_call_method(ass->rtpsend, MS_RTP_SEND_UNMUTE, event_arg);
    }
}