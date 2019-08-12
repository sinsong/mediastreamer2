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

static void on_silence_detected(void *data, MSFilter *f, unsigned int event_id, void *event_arg);

int main(void)
{
	int rate=8000;
	MSSndCard *card_capture;
	MSSndCard *card_playback;
	MSFactory *factory;
	MSFilter *capture;
	MSFilter *playback;
	MSFilter *vaddtx;
	MSFilter *encoder;
	MSFilter *decoder;
	MSFilter *rtprecv;
	MSFilter *rtpsend;
	MSTicker *ticker;
	PayloadType *pt;
	RtpProfile *profile;
	RtpSession *rtps;
	char ip[64]="127.0.0.1";
	int port;
	int payload;
	
	payload = 0;
	port=1200;

	//configure ticker ,factory and rtps
	ticker = ms_ticker_new();
	factory = ms_factory_new_with_voip();
	rtps=rtp_session_new(RTP_SESSION_SENDRECV);
	
	//find capture and playback
	card_capture = ms_snd_card_manager_get_default_capture_card(ms_factory_get_snd_card_manager(factory));
	card_playback = ms_snd_card_manager_get_default_playback_card(ms_factory_get_snd_card_manager(factory));

	//init
	ortp_init();
	ortp_set_log_level_mask(ORTP_LOG_DOMAIN, ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR|ORTP_FATAL);
	profile=rtp_profile_clone_full(&av_profile);
	rtp_profile_set_payload(profile,120,&payload_type_opus);
	pt=rtp_profile_get_payload(profile,payload);
	ms_factory_init_voip(factory);
	ms_factory_init_plugins(factory);
	
	//set local and remote addr
	rtp_session_set_local_addr(rtps,"0.0.0.0",1200,1201);
	rtp_session_set_remote_addr_full(rtps,ip,port,ip,port);
	
	//make filters	
	capture=ms_snd_card_create_reader(card_capture);
	playback=ms_snd_card_create_writer(card_playback);
	vaddtx=ms_factory_create_filter(ass->factory, MS_VAD_DTX_ID);
	encoder=ms_factory_create_encoder(factory,pt->mime_type);
	decoder=ms_factory_create_decoder(factory,pt->mime_type);
	rtprecv=ms_factory_create_filter(factory,MS_RTP_RECV_ID);
	rtpsend=ms_factory_create_filter(factory,MS_RTP_SEND_ID);
	
	//configure filters
	ms_filter_call_method (capture, MS_FILTER_SET_SAMPLE_RATE,&rate);
	ms_filter_call_method (playback, MS_FILTER_SET_SAMPLE_RATE,&rate);
	ms_filter_call_method (vaddtx, on_silence_detected, rtpsend);
	ms_filter_call_method (encoder,MS_FILTER_SET_SAMPLE_RATE,&rate);
	ms_filter_call_method (decoder,MS_FILTER_SET_SAMPLE_RATE,&rate);
	ms_filter_call_method (rtprecv,MS_RTP_RECV_SET_SESSION,rtps);
	ms_filter_call_method (rtpsend,MS_RTP_SEND_SET_SESSION,rtps);
	ms_filter_call_method (rtprecv, MS_FILTER_SET_SAMPLE_RATE,&pt->clock_rate);

	//link filters 
	ms_filter_link(capture,0, encoder,0);
	ms_filter_link(encoder, 0, vaddtx, 0);
	ms_filter_link(vaddtx, 0, rtpsend,0);

	ms_filter_link(rtprecv,0, decoder,0);
	ms_filter_link(decoder,0, playback,0);
	ms_ticker_attach(ticker, capture);
	ms_ticker_attach(ticker, rtprecv);

	ms_sleep(15);

	//return to the world
	ms_ticker_detach(ticker,capture);
	ms_ticker_detach(ticker,rtprecv);
	ms_ticker_destroy(ticker);
	ms_filter_unlink(capture,0, encoder,0);
	ms_filter_unlink(encoder, 0, vaddtx, 0);
	ms_filter_unlink(vaddtx, 0, rtpsend,0);
	ms_filter_unlink(rtprecv,0, decoder,0);
	ms_filter_unlink(decoder,0, playback,0);
	ms_filter_destroy(capture);
	ms_filter_destroy(playback);
	ms_filter_destroy(vaddtx);
	ms_filter_destroy(decoder);
	ms_filter_destroy(encoder);
	ms_filter_destroy(rtprecv);
	ms_filter_destroy(rtpsend);	
	ms_factory_destroy(factory);
	return 0;
	
}

static void on_silence_detected(void *data, MSFilter *f, unsigned int event_id, void *event_arg)
{
    MSFilter *rtpsend = (MSFilter*)data;
    switch(event_id)
    {
    case MS_VAD_DTX_NO_VOICE:
        ms_message("vaddtx: voice X - XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
        //ms_filter_call_method(ass->rtpsend, MS_RTP_SEND_SEND_GENERIC_CN, event_arg);
        ms_filter_call_method(rtpsend, MS_RTP_SEND_MUTE, event_arg);
        break;
    case MS_VAD_DTX_VOICE:
        ms_message("vaddtx: voice R - $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$");
        ms_filter_call_method(rtpsend, MS_RTP_SEND_UNMUTE, event_arg);
    }
}