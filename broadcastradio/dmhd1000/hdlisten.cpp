#include <iostream>
#include <string>
#include <map>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <android/log.h>
#include <cutils/properties.h>
#include "hddefs.h"
#include "hdlinuxio.h"
#include "hdlisten.h"
using namespace std;

#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"RADIO",__VA_ARGS__)

/**
 * Constructor to set vals for the listener.  After the listener is constructed, it still
 * needs the definitions and the port.  Once those are provided through setdefs() and setioport(),
 * call listentoradio().  This will start a thread that will listen for any input from the radio
 * and process it.  After that is working, call gethdval(), gethdintval(), or gethdboolval to get
 * any values we have received from the radio.  If a value does not exist or has not been set
 * by the radio yet, then an empty string will be returned.  (If an int or bool value has not been
 * set a -1 or false will be returned.)
 *
 * Every time the station or frequency is changed, the station dependent values (like station name or
 * hdtitle) will be set to an empty string.  In most cases this information will be reset quickly on
 * reaching a new, valid station.
 *
 * The title and artist info is kept for all HD channels supplied for the current station.  The values
 * for "hdtitle" and "hdartist" will alwyas reflect the info for the current subchannel.
 */
HDListen::HDListen() {//public
	verbose = false;
	keepReading = true; havecode = false; valueset = false; escChar = false; lengthWait = false;
	msglen = -1; msgin = -1;
	lastsubchannelcount = 0;
	naptime = 100;
	ctype = "";
	lasttune = "";
	valueset = true;
	listenThread = 0;
	cktotal = 0;
	ioport = 0;
	hdvals = 0;
	bq = (int *)malloc(sizeof(int)*1024);
	radiovals["initialized"] = "true";
	return;
}

/**
 * Set the verbosity of this class to true if debugging statements are desired.
 * @param verbosity true for debugging output to the console
 */
void HDListen::setverbose(bool verbosity) {//public
	verbose = verbosity;
	return;
}

/**
 * Provide the class full of common variables.
 * @param hdv the HDValue object we use for common variables
 */
void HDListen::setdefs(HDVals* hdv) {//public
	hdvals = hdv;
	return;
}

/**
 * Provide the port so we can read data from it.
 * @param iop the LinuxPort class
 */
void HDListen::setioport(LinuxPort* iop) {//public
	ioport = iop;
	return;
}

/**
 * Public only because it has to be accessed from outside the class, but it would
 * not be if not so.  Used to start the independent thread to listen to the radio port.
 */
void HDListen::listenthread() {//public
	if (verbose) cout << "Listening thread started...\n";
	readinfile();
	ioport->closeport();
	return;
}

/**
 * Call here to start the listener.  It spins off a separate thread to listen to
 * the radio output.
 */
void HDListen::listentoradio() {//public
	if (listenThread == 0)
		pthread_create(&listenThread, NULL, StartHDListener, this);
	return;
}

/**
 * Get a value from our settings.  These values are set from the data retrieved from the
 * radio.  There is no guarentee that all the values will be set because they may not have
 * been received.  All get routines use this to get the original value before converting
 * it to a bool or int.  This is thread safe (uses a pthread_mutex) so there won't be
 * problems with getting a value that is in the process of being set.
 * @param hdkey the name of the value to get
 * @return the value requested
 */
string HDListen::gethdvalue(string hdkey) {//public
	//pthread_mutex_lock(&valLock);
	string hdval = radiovals[hdkey];
	//pthread_mutex_unlock(&valLock);
	return hdval;
}

/**
 * Get an int value from our settings.
 * @param hdkey the name of the value to get
 * @return the value requested
 */
int HDListen::gethdintval(string hdkey) {//public
	int ival;
	string sval = gethdvalue(hdkey);
	if (sval == "") return -1;
	sscanf(sval.c_str(), "%d", &ival);
	return ival;
}

/**
 * Get a boolean value from our settings.
 * @param hdkey the name of the value to get
 * @return the value requested
 */
bool HDListen::gethdboolval(string hdkey) {//public
	bool bval = false;
	string sval = gethdvalue(hdkey);
	if (sval == "true")
		bval = true;
	return bval;
}

/**
 * Set the HD title for a specific channel.
 * @param channel channel to set the title for
 * @param title new title info
 */
void HDListen::sethdtitle(int channel, string title) {//private;
	hdtitles[channel] = title;
	return;
}

/**
 * Get the title for a particular hd subchannel
 * @param channel the subchannel to get the title for
 * @return the title name on the specified hd channel
 */
string HDListen::gethdtitle(int channel) {//public
	return hdtitles[channel];
}

/**
 * Get a list of all the hdtitle tags current for this station.
 * @return the list of titles on all the current tracks as a barftor
 */
map<int,string> HDListen::gethdtitles() {//public
	return hdtitles;
}

/**
 * Set the HD artist for a specific channel.
 * @param channel channel to set the artist for
 * @param artist artist info
 */
void HDListen::sethdartist(int channel, string artist) {//private;
	hdartists[channel] = artist;
	return;
}

/**
 * Get the artist for a particular hd subchannel
 * @param channel the subchannel to get the artist for
 * @return the artist name on the specified hd channel
 */
string HDListen::gethdartist(int channel) {//public
	return hdartists[channel];
}

/*	*
 * Get a list of all the hdartist tags current for this station.
 * @return the list of artists on all the current tracks as a barftor
 */
map<int,string> HDListen::gethdartists() {//public
	return hdartists;
}

/**
 * Process a complete message we've received from the radio.  See what kind of format it is in,
 * what the message name is, and what the data is.  Store the result as the proper key/value pair
 * in the map of values.
 */
string HDListen::decodemsg(unsigned char *message) {//protected
	int ival;
	string msgname, msgfmt, msgval = "", val1, val2, valx;
	uint32_t len = 0;
	uint32_t i;

	char mesbuf[4096];
	mesbuf[0] = 0;

	for (i=0; i<message[1]+3; i++){
		sprintf(mesbuf+strlen(mesbuf), "%02X ", message[i]);
	}
	LOGD("Received BUFFER: %s", mesbuf);

	msgname = hdvals->getcmd(message[2], message[3]);
	msgfmt = hdvals->getformat(msgname);
	if (msgfmt == "boolean") {
		if (msgval == hdvals->getconstant("one"))
			msgval = "true";
		else
			msgval = "false";
	} else if (msgfmt == "int") {
		// A4 08 01 01 02 00 2C 01 00 00
		// A4: start of message
		// 08: length
		// 01 01: signal strength
		// 02 00: reply
		// 2C 01: value (little endian)
		// 00 00: ???
		ival = ((int)message[6] + (((int)message[7])<<8));
		sprintf(curmsg, "%d", ival);
		msgval = curmsg;
	} else if (msgfmt == "string") {
		// A4: header
		// 10: packet length
		// 08 03: RDS PS
		// 02 00: response
		// 08 00 00 00: string length
		// 20 20 38 38 2E 35 20 20 "  88.5  "
		// 1C: checksum

		len = ((uint32_t)message[6]) + (((uint32_t)message[7])<<8) + (((uint32_t)message[8])<<16) + (((uint32_t)message[9])<<24);
		for (i=10; i<len+10; i++) msgval += message[i];
	} else if (msgfmt == "band:int") {
		// Received BUFFER: A4 14 03 01 02 00 01 00 00 00 CF 03 00 00 00 00 00 00 00 00 00 00 91
		// A4: header
		// 14: length
		// 03 01: seek
		// 02 00: response
		// 01 00 00 00: FM (00 00 00 00 = AM)
		// CF 03 ....: freq little-endian 0x03cf = 975 = 97.5 MHz

		if (message[6] == 0x01){ // FM
			sprintf(curmsg, "%d FM", (int)message[10] + ((int)message[11]<<8));
		} else { // AM
			sprintf(curmsg, "%d AM", (int)message[10] + ((int)message[11]<<8));
		}
		msgval = curmsg;

	/* Lets just disable HD radio altogether. It can only cause annoyance.
	} else if (msgfmt == "int:string") {
		if (currentsubchannel < 1) {
			cout << "Setting subchannel to 1\n";
			currentsubchannel = 1;
			sethdval("hdsubchannel", "1");
		}
		val1 = msgval.substr(0, 19);
		val2 = msgval.substr(40, msgval.size());
		ival = hexbytestoint(val1);
		msgval = hexbytestostring(val2);
		sprintf(xfer, "%d", ival);
		val1 = xfer;
		if (msgname == "hdtitle")
			sethdtitle(ival, msgval);
		if (msgname == "hdartist")
			sethdartist(ival, msgval);
		if (verbose) cout << "Message name(a): " << msgname << ":" << val1 << ", Value: \"" << msgval << "\"" << endl;
		if (currentsubchannel != ival) {
			return msgval;
		}
	*/
	} else if (msgfmt == "none" || msgfmt == "int:string") {
		msgval = "";
	}
	LOGD("Message name: %s, Value: %s",msgname.c_str(),msgval.c_str());

	callback(msgname,msgval);
	return msgval;
}

void HDListen::passCB(
		android::hardware::broadcastradio::V1_1::ProgramSelector in_ps,
		android::hardware::broadcastradio::V1_1::ProgramInfo in_pi,
		const android::sp<android::hardware::broadcastradio::V1_0::ITunerCallback>& in_cb
	){
	ps = in_ps;
	pi = in_pi;
	cb = in_cb;
	nocb = false;
}

void HDListen::callback(string name, string val){

/*
 * I've seen these;
 *
 * volume (0-100)
 * power (true / false)
 * tune (freq BAND)
 * signalstrength (bignumber like 1536)
 * rdsenable (true / false)
 * rdsprogramservice (text) :: key RDS_PS
 * rdsgenre (text) :: key GENRE
 * rdsradiotext (text) :: key RDS_RT
 * seek (freq BAND)
 * tune (freq BAND)
 */

	LOGD("CALLBACK: %s / %s", name.c_str(), val.c_str());
	bool fm = false;
	bool rds = false;
	int freq = 0;
	char prop[PROP_VALUE_MAX];
	android::sp<android::hardware::broadcastradio::V1_1::ITunerCallback> mCB = nullptr;
	property_get("service.broadcastradio.on", prop, "0");
	LOGD("Read property service.broadcastradio.on = %s", prop);
	
	if (prop[0] == '1' && nocb != true){
		mCB = android::hardware::broadcastradio::V1_1::ITunerCallback::castFrom(cb).withDefault(nullptr);
		if (strcmp(name.c_str(), "seek") == 0){
			fm = (strstr(val.c_str(), "FM") != NULL);
			freq = (int)(atof(val.c_str()) * (fm?100:1));
			if (fm && (freq < 85000 || freq > 109000)) return;
			if (!fm && (freq < 500 || freq > 1800)) return;
			pi.base.channel = freq;
			ps.primaryId.type = 1;
			ps.primaryId.value = freq;
			pi.selector = ps;
			pi.base.tuned = 0;
			pi.base.stereo = 1;
			pi.base.digital = 0;
			pi.base.signalStrength = 50;
			mCB->currentProgramInfoChanged(pi);
		} else if (strcmp(name.c_str(), "tune") == 0){
			rds_ps = "";
			rds_rt = "";
			rds_genre = "";
			fm = (strstr(val.c_str(), "FM") != NULL);
			freq = (int)(atof(val.c_str()) * (fm?100:1));
			if (fm && (freq < 85000 || freq > 109000)) return;
			if (!fm && (freq < 500 || freq > 1800)) return;
			pi.base.channel = freq;
			ps.primaryId.type = 1;
			ps.primaryId.value = freq;
			pi.selector = ps;
			pi.base.tuned = 1;
			pi.base.stereo = 1;
			pi.base.digital = 0;
			pi.base.signalStrength = 50;
			mCB->tuneComplete_1_1(android::hardware::broadcastradio::V1_0::Result::OK, pi.selector);
			rds = true;
		} else if (strcmp(name.c_str(), "rdsprogramservice") == 0){
			rds_ps = val;
			rds = true;
		} else if (strcmp(name.c_str(), "rdsradiotext") == 0){
			rds_rt = val;
			rds = true;
		} else if (strcmp(name.c_str(), "rdsgenre") == 0){
			rds_genre = val;
			rds = true;
		} else if (strcmp(name.c_str(), "signalstrength") == 0){
			pi.base.signalStrength = atoi(val.c_str());
			if (pi.base.signalStrength < 400) pi.base.signalStrength = 0;
			else if (pi.base.signalStrength > 2850) pi.base.signalStrength = 100;
			else pi.base.signalStrength = (int)(((float) (pi.base.signalStrength - 400) / (float) 2450) * 100);
			mCB->currentProgramInfoChanged(pi);
		}
		if (rds) {
			pi.base.metadata = android::hardware::hidl_vec<android::hardware::broadcastradio::V1_0::MetaData>(3);
			pi.base.metadata[0] = {
				android::hardware::broadcastradio::V1_0::MetadataType::TEXT,
				android::hardware::broadcastradio::V1_0::MetadataKey::RDS_PS,
				{},
				{},
				rds_ps,
				{}
			};
			pi.base.metadata[1] = {
				android::hardware::broadcastradio::V1_0::MetadataType::TEXT,
				android::hardware::broadcastradio::V1_0::MetadataKey::TITLE,//RDS_RT,
				{},
				{},
				rds_rt,
				{}
			};
			pi.base.metadata[2] = {
				android::hardware::broadcastradio::V1_0::MetadataType::TEXT,
				android::hardware::broadcastradio::V1_0::MetadataKey::GENRE,
				{},
				{},
				rds_genre,
				{}
			};
			mCB->currentProgramInfoChanged(pi);
		}
	} else nocb = true;

	if (cb == nullptr){
		LOGD("CB is nullptr");
	}
}

/**
 * Read input from the serial port
 */
void HDListen::readinfile() {//protected
	unsigned char *buff;
	while (keepReading) {
		buff = ioport->hdreadbytes(1);
		if (buff != NULL) decodemsg(buff);
	}
	return;
}

/**
 * Set the flag to tell the listener to stop reading from the port.
 */
void HDListen::stopreading() {//public
	keepReading = false;
	listenThread = 0;
	return;
}

/**
 * Used outside of the class to provide a way to call HDListen::listenthreaad() to
 * start a separate listener thread.
 */
void *StartHDListener(void* ctx) {
	HDListen *hdl = static_cast<HDListen *>(ctx);
	hdl->listenthread();
	return 0;
}

