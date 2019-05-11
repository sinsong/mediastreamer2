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

static void tmmbr_received(const OrtpEventData *evd, void *user_pointer);

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

AudioStreamSpecialization *ass = NULL;

int main()
{
    lport = 20020;

    strcpy(ip, "127.0.0.1");
    rport = 20020;

    printf("[%d] -> [%s:%d]", lport, ip, rport);

    payload = 121;
    session = NULL;
    q = NULL;
    pt = NULL;
    profile = NULL;

    ortp_init();
    
    factory = ms_factory_new_with_voip();

    rtp_profile_set_payload(profile, 121, &payload_type_opus);
    pt = rtp_profile_get_payload(profile, payload);

    MSSndCardManager *manager = ms_factory_get_snd_card_manager(factory);
	MSSndCard *capt = ms_snd_card_manager_get_default_capture_card(manager);
	MSSndCard *play = ms_snd_card_manager_get_default_capture_card(manager);
    ass = audio_stream_specialization(factory, lport, lport + 1);

    if (capt)
		ms_snd_card_set_preferred_sample_rate(capt,rtp_profile_get_payload(profile, payload)->clock_rate);
	if (play)
		ms_snd_card_set_preferred_sample_rate(play,rtp_profile_get_payload(profile, payload)->clock_rate);

    audio_stream_specialization_start(ass, profile, ip, rport, ip, rport+1, payload, capt, play);

    // 开始
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
    if(ass->mixer) ms_filter_destroy(ass->mixer);

    ms_free(ass); // memory free
}

AudioStreamSpecialization *audio_stream_specialization(MSFactory *factory, int lport, int lport_rtcp)
{
    AudioStreamSpecialization *ass;

    RtpSession *session;
    session = rtp_session_new(RTP_SESSION_SENDRECV);
    rtp_session_set_local_addr(session, "0.0.0.0", lport, lport_rtcp); // 本地地址
    //disable_checksums(rtp_session_get_rtp_socket(rtpr));

    ass = (AudioStreamSpecialization *)ms_new0(AudioStreamSpecialization, 1);

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
    rtp_session_enable(rtps, TRUE); // 如果rtcp_port是0就设置为FALSE

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

    ass->encoder = ms_factory_create_encoder(ass->factory, pt->mime_type);
    ass->decoder = ms_factory_create_decoder(ass->factory, pt->mime_type);

    ass->mixer = ms_factory_create_filter(ass->factory, MS_AUDIO_MIXER_ID);

    ass->ticker = ms_ticker_new();

    ms_connection_helper_start(&h);
    ms_connection_helper_link(&h, ass->soundread, -1, 0);
    if(ass->ec) ms_connection_helper_link(&h, ass->ec, 0, 0);
    ms_connection_helper_link(&h, ass->encoder, 0, 0);
    ms_connection_helper_link(&h, ass->rtpsend, 0, -1);

    ms_connection_helper_start(&h);
    ms_connection_helper_link(&h, ass->rtprecv, -1, 0);
    ms_connection_helper_link(&h, ass->decoder, 0, 0);
    ms_connection_helper_link(&h, ass->mixer, 0, 0);
    ms_connection_helper_link(&h, ass->soundwrite, 0, -1);

    ms_ticker_attach_multiple(ass->ticker, ass->soundread, ass->rtprecv);

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