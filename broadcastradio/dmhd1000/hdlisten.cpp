/*
 * HD Radio Controller
 * (c) Hal Vaughan 2008
 * hal@halblog.com
 * licensed under the Free Software Foundations General Public License 2.0
 *
 * This program makes it possible to control several different HD and satellite
 * radios from Linux.  It can be used for simple command line control as well as
 * control from a more sophisticated program using this interface as a library.
 *
 * Control protocols provided by Paul Cotter.
 */

//Most of these are not needed if we use only the fileio function
// instead of the portio function.
#include <iostream>
#include <string>
#include <map>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <android/log.h>
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
//	j_cls = 0;
//	s_Jvm = 0;
	hdvals = 0;
	//bq=0;
	//TODO I'm not crazy about this;
	bq = (int *)malloc(sizeof(int)*1024);
	radiovals["initialized"] = "true";
//DEBUG:
// 	verbose = true;
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
 * Simple routine to output the character in a readable 0x00 format,
 * followed by an int version, followed by a printable character (if
 * it is printable).  There is one subtlity: if we have a code for the
 * current characters (as in we know we're getting specific data), then
 * the separator between the hex and dec numbers is ":", if we don't have
 * a code, it's "-".  The A4 code saying we're starting data will always
 * have a "-" in it.
 * @param cIn the character to print
 */
void HDListen::chout(unsigned char cIn) {//protected

	char chex [10];
	int iIn;

	iIn = cIn;
	if (iIn == 0xA4) {
		cout << endl << endl;
	}
	sprintf(chex, "0x%2X", cIn);
	if (chex[2] == ' ')
		chex[2] = '0';
	if (havecode)
		cout << chex << ":" << iIn << " [";
	else
		cout << chex << "-" << iIn << " [";
	if (iIn >= 32 && iIn <= 126) {
		cout << cIn;
	}
	cout << "] ";
// 	cout << endl;
	return;
}

/**
 * Convert a string of hex represented bytes (like "0xAC 0x1C") into an integer.
 * @param inbytes the string to be converted to an integer
 * @return the integer
 */
int HDListen::hexbytestoint(string inbytes) {//protected
	int i1, i2, i3, i4;
	long ans;
	string xfer = "0x";
	if (inbytes.size() > 19)
		inbytes = inbytes.substr(0, 19);
	sscanf(inbytes.c_str(), "%X %X %X %X", &i1, &i2, &i3, &i4);
	ans = i1 + (256 * i2) + (65536 * i3) + (16777216 * i4);
	if (ans > 65535)
		i1 = -1;
	else
		i1 = ans;
	return i1;
}

/**
 * Convert a string of hex represented bytes in the form of "0xAB 0x12" and
 * convert it into a string of readable characters.
 * @param inbytes the string to convert
 * @return the converted string
 */
string HDListen::hexbytestostring(string inbytes) {//protected
	char c;
	int ival;
	unsigned int istart;
	string xfer, sval = "";

	istart = 0;
	curmsg[0] = 000;
	while (istart < inbytes.size() - 3) {
		xfer = inbytes.substr(istart, 4);
		sscanf(xfer.c_str(), "%X", &ival);
		c = ival;
		sval += c;
		istart += 5;
	}
	return sval;
}

/**
 * Process a complete message we've received from the radio.  See what kind of format it is in,
 * what the message name is, and what the data is.  Store the result as the proper key/value pair
 * in the map of values.
 */
string HDListen::decodemsg() {//protected
	char xfer[10];
	int ival;
	string msgname, msgfmt, msgval = "", val1, val2, valx;
	msgname = hdvals->getcommand(msgcode);
	msgfmt = hdvals->getformat(msgname);
	msgval = curmsg;
	if (msgfmt == "boolean") {
//LOGD("DECODEMSG: boolean");
		if (msgval == hdvals->getconstant("one"))
			msgval = "true";
		else
			msgval = "false";
	} else if (msgfmt == "int") {
//LOGD("DECODEMSG: int");
		ival = hexbytestoint(msgval);
		if (hdvals->getscaled(msgname)) {
			ival = (ival * 100)/90;
		}
		sprintf(curmsg, "%d", ival);
		msgval = curmsg;
	} else if (msgfmt == "string") {
//LOGD("DECODEMSG: string");
		msgval = curmsg;
		msgval = msgval.substr(20, msgval.size() - 20);
		msgval = hexbytestostring(msgval);
	} else if (msgfmt == "band:int") {
//LOGD("DECODEMSG: band:int");
		msgval = curmsg;
		val1 = msgval.substr(0, 19);
		val2 = msgval.substr(20, 19);
		ival = hexbytestoint(val2);
		sprintf(xfer, "%d", ival);
		msgval = xfer;
		currfreq = xfer;
		val2 = msgval.substr(msgval.size() - 1, 1);
		msgval = msgval.substr(0, msgval.size() - 1);
		if (val1 == hdvals->getband("am")) {
			val1 = "AM";
			currband = "am";
		} else {
			val1 = "FM";
			currband = "fm";
			msgval += ".";
		}
		msgval += val2;
		msgval += " ";
		msgval += val1;
/* Lets just disable HD radio altogether. It can only cause annoyance.
	} else if (msgfmt == "int:string") {
		if (currentsubchannel < 1) {
// 			cout << "Setting subchannel to 1\n";
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
//LOGD("DECODEMSG: int:string");
		msgval = "";
	}
//	LOGD("Message name: %s, Value: %s",msgname.c_str(),msgval.c_str());
	//sethdval(msgname, msgval);

	//here send it up to java.
	callback(msgname,msgval);
	return msgval;
}

/*
void HDListen::passJvm(JavaVM * jvm, jclass jcls){
	s_Jvm = jvm;
	j_cls = jcls;
}
*/

void HDListen::callback(string name, string val){
/* TODO This needs to be redone
	JNIEnv * env;
//	LOGD("CALLBACK RUNNING");

	if (s_Jvm != NULL){
		s_Jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
	}

	s_Jvm->AttachCurrentThread(&env,NULL);
	jmethodID j_mid = env->GetStaticMethodID(j_cls,"radioCallback", "(Ljava/lang/String;Ljava/lang/String;)V");
	jstring jname = env->NewStringUTF(name.c_str());
	jstring jval = env->NewStringUTF(val.c_str());
	env->CallStaticVoidMethod(j_cls,j_mid,jname,jval);
	env->DeleteLocalRef(jname);
	env->DeleteLocalRef(jval);
*/
name="use this variable";
val=4;
}

/**
 * Take the bytes that have come in for an entire message and do the basic processing
 * to put them in a form that can be easily decoded.
 */
void HDListen::procmsg() {//protected
	int x, y, num, cp = 0, t;
	char chex[6] = {"0x%2X"};
	sprintf(msgcode, "0x%2X 0x%2X", bq[0], bq[1]);
	sprintf(msgtype, "0x%2X 0x%2X", bq[2], bq[3]);
	for (num = 2; num < 8; num += 5) {
		if (msgcode[num] == ' ') {
			msgcode[num] = '0';
		}
		if (msgtype[num] == ' ') {
			msgtype[num] = '0';
		}
	}
// 	cout << "Message type: " << msgtype << ", Message code: " << msgcode << endl;
//Don't bother with set or get commands that won't give us data.
	if (strcmp(msgtype,"0x00 0x00") == 0 || strcmp(msgtype,"0x01 0x00") == 0)
		return;
	currentmsg = "";
	curmsg[0] = 000;
	cp = 0;
	for (x = 4; x < msglen; x++) {
		num = bq[x];
		if (cp != 0) {
			curmsg[cp++] = ' ';
		}
		t = cp + 2;
		y = 0;
		do {
			curmsg[cp + y] = chex[y];
			y++;
		} while (chex[y] != 000);
		cp = t + 2;
		curmsg[cp + 2] = 000;
		sprintf(curmsg, curmsg, num);
		if (curmsg[t] == ' ')
			curmsg[t] = '0';
		curmsg[cp] = 000;
	}
	usleep(naptime);
// 	LOGD("Message... code: %s, type: %s, value: %s",msgcode,msgtype,curmsg);
	decodemsg();
	return;
}

/**
 * Process an incoming character.  If it's 0xA4 and not escaped, it's part of an incoming message.
 * Also check for bytes indicating message length, the checksum at the end of the message, and so on.
 * Basically make sure we get an entire message and when we do, pass it on for further processing to
 * determine the type of message and content.  If a message is incomplete or doesn't have the right
 * checksum (rare), then it is just discarded.  When we call procmessage() we don't pass it on, since
 * the variables need to be accessed by different subroutines, they're global to this class.
 * @param cIn a character to be processed as part of the incoming stream of data from the radio.
 */
void HDListen::handlebyte(unsigned char cIn) {//protected
	unsigned int cksum;

	
//LOGD("Processing byte: %x",cIn);
// 	cout << "Processing byte: ";
// 	chout(cIn);
	if (cIn != 0xA4 && !havecode) {
// 		cout << "Received byte without reply active\n";
		return;
	}
// 	cout << "Length wait: " << lengthWait << ", Msgin: " << msgin << ", Msglen: " << msglen << endl;
	if (cIn == 0xA4 && !lengthWait && !(msgin == msglen && msglen >= 1)) {
		
		if (havecode) {
			if (verbose) cout << "New reply code received in middle of message.  Discarding data and restarting.\n";
		} else {
 			//cout << "New reply code starting from scratch\n";
		}
		havecode = true;
		msglen = 0;
		msgin = 0;
//		if (bq != 0) free(&bq);
		cktotal = 0xA4;
		currentmsg[0] = 00;
		lengthWait = true;
		escChar = false;
		return;
 		cout << "New message flag received. ";
	}
	if (msglen == 0) {
		msglen = cIn;
// 		cout << "Setting message length: " << msglen << " ";
		cktotal += cIn;
		lengthWait = false;
		return;
	}
	lengthWait = false;
// 	cout << "Message length: " << msglen << ", Message in: " << msgin << endl;
	if (msgin < msglen) {
		if (cIn == 0x1B && !escChar) {
			escChar = true;
			return;
		}
		if (escChar) {
			escChar = false;
			if (cIn == 0x48)
				cIn = 0xA4;
		}
//LOGD("adding byte to bq[%d]:%x",msgin, cIn);
 		bq[msgin] = cIn;
		cktotal += cIn;
 		cout << "Current checksum: " << cktotal << endl;
		msgin++;
		return;
 		cout << "\tMessage in: " << msgin << endl;
	} else if (msgin == msglen) {
		havecode = false;
		cksum = cIn;
		cout << "Raw checksum: " << cktotal << ", Received byte: " << cksum << ", ";
		cktotal = cktotal % 256;
		cout << "Our checksum: " << cktotal << ", Their checksum: " << cksum << endl;

		// The checksum calculation routines are *WRONG*, therefore ignore and process the message anyway.
		procmsg();

		msglen = -1;
		msgin = -1;
	}
	if (msgin > msglen) {
		if (verbose) cout << "Error: Received message did not match given length.  Data discarded\n";
		havecode = false;
	}

	return;
}

/**
 * Read input from the serial port, then pass each byte to the byte handling routine.
 * Every byte that is read in is passed to handlebyte() so it can process it.
 */
void HDListen::readinfile() {//protected
	char *buff = new char[10];
	unsigned char cIn;

//TODO: this does not seem efficient.
	while (keepReading) {
// 		cout << "Start of read loop\n";
		buff = ioport->hdreadbytes(1);
		cIn = buff[0];
//printf("BYTE: 0x%x\n",cIn);
		handlebyte(cIn);
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

