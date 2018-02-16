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

#include <string>
#include <stdio.h>
#include <android/log.h>
#include <jni.h>
#include "hdlinuxio.h"
#include "hdcommands.h"
#include "hdlisten.h"
#include "hddefs.h"
#include "hdcontrol.h"

#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"RADIO",__VA_ARGS__)

/**
 * This is the main control class for controlling HD Radios and for getting data back from them
 * to determine their current state.  It depends on other classes, but the entire interface for all
 * functions and settings is here.  There is no need to create any other classes, all that is done
 * automatically.
 *
 * By default the system config file is /ect/HRDadio.cfg and there is a user config file in the
 * home directory .hdradio/HDRadioSession.cfg.  The user file overrides the system one and includes
 * user specific settings including barforite channels.
 *
 * It is important to note there is a difference between commands that allow one to get settings like
 * the volume level from the radio and the ones that get data to return.  While almost every time a
 * command asking for data is sent to the radio it responds immediately, that is not guarenteed to be
 * the case.  To solve this problem, there is a thread that runs continuously getting any data returned
 * by the radio and storing it in our own structure.  Getting a value in our structure returns the current
 * value.  Getting it form the radio tells the radio to send it to us so we can have a current value.
 *
 * The commands that ask the radio for data start with "request_" and the ones that get data from our
 * settings start with "get_".
 *
 * The current setting of the radio is saved in the user config file whenever the radio is tuned or certain
 * other changes are made so when the program is run again, the radio can be returned to the last saved state.
 * (This is not automatic, since it might not always be wanted.)
 *
 * Be sure to look at the info about autodiscovery in the linuxio.cpp file, which includs the LinuxPort object.
 * Autodiscovery helps find the radio and provides the current serial port, but it can also cause problems with
 * other devices communicating through serial ports.  To avoid most problems with autodiscovery, once the radio
 * is found on a particular device, that device is saved in the config file.
 *
 * The constructor does not start this class, it just sets it up.  To start the radio connection, call activate().
 * By separating the functions, that allows modification and changes in settings from class creation until actual
 * startup and use.  For example, command line args can be passed on to override config settings, then activate()
 * can be called.
 */
HDControl::HDControl() {//public
	verbose = false;
	mainconfigfile="/etc/HDRadio.cfg";
	defaultserial = "/dev/ttyUSB0";
	defstreamwait = 1;
	deffreq = "931";
	defband = "fm";
	defvol = "50";
	defbass = "50";
	deftreb = "50";

//Save list of settings to keep when we save the state.
//This is pointless.

// 	setverbose(true);
	hdListen.setioport(&ioPort);
	hdListen.setdefs(&hdValues);
	hdCommand.setioport(&ioPort);
	hdCommand.setdefs(&hdValues);
	hdCommand.setlisten(&hdListen);

	return;
}

/**
 * Specify the serial device to use.  If done after the command line args are
 * parsed, it'll over ride any command line or config file speficiations.  Otherwise,
 * the device is specifed from the config file.
 * @param serialDevice the serial device to use
 */
void HDControl::setSerialPort(string serialDevice) {//public
	if (serialDevice != "") {
		defaultserial = serialDevice;
	}
	return;
}

/**
 * Actually start the communications with the device.  If we've never been run before on this
 * computer, then find the radio.  Also start the listener thread to make sure the output of
 * the device is processed.
 */
void HDControl::activate() {//public
LOGD("RUNNING activate()");
	hdCommand.setstreamwait(defstreamwait);
	ioPort.setserialport(defaultserial);
	ioPort.openport();
	ioPort.hanguponexit(false);
	hdListen.listentoradio();
	//command_line("requestpower");
/*	if (true) {
		request_power();
		request_mute();
		request_volume();
		request_bass();
		request_treble();
		request_tune();
		request_hdsubchannel();
	}*/

	return;
}

/*
void HDControl::passJvm(JavaVM * jvm, jclass jcls){
	hdListen.passJvm(jvm, jcls);
}
*/
void HDControl::passCB(
		android::hardware::broadcastradio::V1_1::ProgramSelector ps,
		android::hardware::broadcastradio::V1_1::ProgramInfo pi,
		const android::sp<android::hardware::broadcastradio::V1_0::ITunerCallback>& cb
//		android::sp<android::hardware::broadcastradio::V1_1::ITunerCallback> cb
	){
	hdListen.passCB(ps, pi, cb);
}


/**
 * Close the port to the radio.
 */
void HDControl::close() {//public
LOGD("RUNNING close()");
	hdListen.stopreading();
	ioPort.closeport();
	return;
}

/**
 * Set verbosity.  True means debugging messages will be sent to the console.
 * @param verbosity set to true to get debugging messages
 */
void HDControl::setVerbose(bool verbosity) {//public
	verbose = verbosity;
	ioPort.setverbose(verbose);
	hdListen.setverbose(verbose);
	hdCommand.setverbose(verbose);
	return;
}

/**
 * Load the state from the user config file and set the radio to match
 * that state.  This lets us start up in the same state we were shut down in.
 * TODO: This has to set state based on passed parameters. Otherwise it is
 * completely moronic.
 */
void HDControl::restoreState() {//public
	string key, val;
	radioOn();
	muteOff();
	hd_setvolume(defvol);
	hd_setbass(defbass);
	hd_settreble(deftreb);
//TODO: this really does need to be stored, just not in idiot files.
	hdCommand.hd_tune(deffreq);
//	hd_seekall();
	return;
}

/**
	Parses the command given it.  Args are separated by spaces.  Returns
	false only if command was to exit the radio, which could include turning
	if off, but it's up to the caller to make that decision.  Shutting down
	the program and leaving the radio on could be a Bad Thing (tm) unless
	provisions are made to be sure it can be easily turned off otherwise.

	This allows easy parsing of commands from a keyboard but can also be used
	instead of calling individual commands to automatically count the arguments and
	dispatch the proper command.
	@param commandLine a line, as if typed in from the keyboard, containing a command and arguments
	@return true if it was a valid command with the right number of arguments
*/
bool HDControl::command_line(string commandLine) {//public
	bool iscommand = false;
	string cmdargs [10];
	int i, lasti, argcount = -1;
	string cmd, arg;

LOGD("RUNNING command_line(%s)", commandLine.c_str());

// 	cout << "        Command Line: ->" << CommandLine << "<-, Size: " << CommandLine.size() << "\n";
	if (commandLine == "")
		return true;
// 	cout << "Processing command\n"; verbose = true;
	i = commandLine.find_first_of(" ");
// 		cout << "       Position: " << i << endl;
	cmd = commandLine.substr(0, i);
// 		cout << "        Command: ->" << cmd << "<-\n";

//FOLLOWING LINE DOES NOTHING USEFUL. KILL IT.
	//transform(cmd.begin(), cmd.end(), cmd.begin(), (int(*)(int)) tolower);
// I think that all it was supposed to do, is convert all the characters in cmd to lower case.

	lasti = -1;
	argcount = -1;
	do {
		i = commandLine.find_first_of(" ", lasti + 1);
		if (i < 0) {
			arg = commandLine.substr(lasti + 1, commandLine.size());
			if (argcount >= 0)
				cmdargs[argcount] = arg;
			argcount++;
		} else {
			arg = commandLine.substr(lasti + 1, i - (lasti + 1));
			if (argcount >= 0)
				cmdargs[argcount] = arg;
			argcount++;
		}
		lasti = i;
	} while (i >= 0);
// 	cout << "Command arg count: " << argcount << endl;
	if (verbose) cout << "\tExcuting function: " << cmd << endl;
	iscommand = false;
	switch (argcount) {
		case 0:
			LOGD("Running command(%s)", cmd.c_str());
			iscommand = command(cmd);
			break;
		case 1:
			LOGD("Running command(%s, %s)", cmd.c_str(), cmdargs[0].c_str());
			iscommand = command(cmd, cmdargs[0]);
			break;
		case 2:
			LOGD("Running command(%s, %s, %s)", cmd.c_str(), cmdargs[0].c_str(), cmdargs[1].c_str());
			iscommand = command(cmd, cmdargs[0], cmdargs[1]);
			break;
	}
	if (!iscommand)
		if (verbose) cout << "\tUndefined function.\n";
	return iscommand;
}

/**
 * Dispatch commands with no arguments to the appropriate function.  This and the other
 * command() functions can be used instead of the individual command functions as a quick
 * way to dispatch various commands.
 * @return false if the command given is not a no-argument command.
 */
bool HDControl::command(string cmd) {//public
// 	cout << "Zero args command: " << cmd << endl;
	if (cmd == "on") {
		radioOn();
		return true;
	}
	if (cmd == "off") {
		radioOff();
		return true;
	}
	if (cmd == "muteon") {
		muteOn();
		return true;
	}
	if (cmd == "muteoff") {
		muteOff();
		return true;
	}
	if (cmd == "disablehd") {
		disableHD();
		return true;
	}
	if (cmd == "tunedef") {
		tunetodefault();
		return true;
	}
	if (cmd == "tuneup") {
		hd_tuneup();
		return true;
	}
	if (cmd == "tunedown") {
		hd_tunedown();
		return true;
	}
	if (cmd == "seekup") {
		hd_seekup();
		return true;
	}
	if (cmd == "seekdown") {
		hd_seekdown();
		return true;
	}
	if (cmd == "seekall") {
		hd_seekall();
		return true;
	}
	if (cmd == "seekhd") {
		hd_seekhd();
		return true;
	}

	if (cmd == "requestpower") {
		request_power();
		return true;
	}
	if (cmd == "requestvolume") {
		request_volume();
		return true;
	}
	if (cmd == "requestmute") {
		request_mute();
		return true;
	}
	if (cmd == "requestbass") {
		request_bass();
		return true;
	}
	if (cmd == "requesttreble") {
		request_treble();
		return true;
	}
	if (cmd == "requesttune" || cmd == "requestfrequency" || cmd == "requestband") {
		request_tune();
		return true;
	}
	if (cmd == "requesthdsubchannel") {
		request_hdsubchannel();
		return true;
	}
	if (cmd == "requesthdsubchannelcount") {
		request_hdsubchannelcount();
		return true;
	}
	if (cmd == "requesthdcallsign") {
		request_hdcallsign();
		return true;
	}
	if (cmd == "requesthdstationname") {
		request_hdstationname();
		return true;
	}
	if (cmd == "requestuniqueid") {
		request_hduniqueid();
		return true;
	}
	if (cmd == "requesttitle") {
		request_hdtitle();
		return true;
	}
	if (cmd == "requestartist") {
		request_hdartist();
		return true;
	}
	if (cmd == "requestsignalstrength") {
		request_hdsignalstrenth();
		return true;
	}
	if (cmd == "requeststreamlock") {
		request_hdstreamlock();
		return true;
	}
	if (cmd == "requesthdactive") {
		request_hdactive();
		return true;
	}
	if (cmd == "requesthdtunerenabled") {
		request_hdtunerenabled();
		return true;
	}
	if (cmd == "requestapiversion") {
		request_apiversion();
		return true;
	}
	if (cmd == "requesthwversion") {
		request_hwversion();
		return true;
	}
	if (cmd == "requestrdsenable") {
		request_rdsenable();
		return true;
	}
	if (cmd == "requestrdsservice") {
		request_rdsservice();
		return true;
	}
	if (cmd == "requestrdsradiotext") {
		request_rdstext();
		return true;
	}
	if (cmd == "requestrdsgenre") {
		request_rdsgenre();
		return true;
	}

	if (cmd == "restore") {
		restoreState();
		return true;
	}
	if (cmd == "dtr") {
		showdtr();
		return true;
	}
	return false;
}

/**
 * Dispatch commands with 1 argument to the proper function.
 * @param cmd the command to send to the radio
 * @param arg1 the only argument to pass on with the command
 * @return true if the command was valid with the right number of arguments
 */
bool HDControl::command(string cmd, string arg1) {//public
// 	cout << "One arg command: " << cmd << endl;
	if (cmd == "volume") {
		hd_setvolume(arg1);
		return true;
	}
	if (cmd == "bass") {
		hd_setbass(arg1);
		return true;
	}
	if (cmd == "treble") {
		hd_settreble(arg1);
		return true;
	}
	if (cmd == "hdsubchannel") {
		hd_subchannel(arg1);
		return true;
	}
	if (cmd == "dtr") {
		toggledtr(arg1);
		return true;
	}
	if (cmd == "hangonexit") {
		bool hangup = false;
		if (arg1 == "true")
			hangup = true;
		hanguponexit(hangup);
		return true;
	}
	return false;
}

/**
 * Take an argument given to us with an int as the arg and convert it
 * to a string so it can be processed like all other commands.
 * @param cmd the command to send to the radio
 * @param arg1 the only argument to pass on with the command
 * @return true if the command was valid with the right number of arguments
 */
bool HDControl::command(string cmd, int arg1) {//public
	bool iscommand = false;
	string sarg = intToString(arg1);
	iscommand = command(cmd, sarg);
	return iscommand;
}

/**
 * Dispatch commands with 2 arguments to the proper function.
 * @param cmd the command to send to the radio
 * @param arg1 the first argument to pass on with the command
 * @param arg2 the 2nd argument to pass on
 * @return true if the command was valid with the right number of arguments
 */
bool HDControl::command(string cmd, string arg1, string arg2) {//public
// 	cout << "Two args, command: " << cmd << endl;
	if (cmd == "tune") {
		hdCommand.hd_tune(arg1, arg2);
		return true;
	}
	return false;
}

/**
 * Take an argument given to us with an int as the 1st arg and convert it
 * to a string so it can be processed like all other commands.
 * @param cmd the command to send to the radio
 * @param arg1 the first argument to pass on with the command
 * @param arg2 the 2nd argument to pass on
 * @return true if the command was valid with the right number of arguments
 */
bool HDControl::command(string cmd, int arg1, string arg2) {//public
	bool iscommand = false;
	string sarg = intToString(arg1);
	iscommand = command(cmd, sarg, arg2);
	return iscommand;
}

bool HDControl::tune(int freq, int channel, int band){
	return hdCommand.hd_tune(freq, channel, band);
}

/**
 * Utility function to convert an integer to a string.
 * @param num the number to conver to to a string.
 * @return string form of the integer given us
 */
string HDControl::intToString(int num) {//private
	char xfer[10];
	string result;
	sprintf(xfer, "%u", num);
	result = xfer;
	return result;
}

//----------------------------------------------------------
//Send commands to unit
//----------------------------------------------------------

/**
 * Turn the radio on.
 */
void HDControl::radioOn() {//public
	hdCommand.hd_power("up");
	return;
}

/**
 * Turn the radio off.
 */
void HDControl::radioOff() {//public
	hdCommand.hd_power("zero");
	return;
}

/**
 * Turn the mute on.
 */
void HDControl::muteOn() {//public
	hdCommand.hd_mute("up");
	return;
}

/**
 * Turn the mute off
 */
void HDControl::muteOff() {//public
	hdCommand.hd_mute("zero");
	return;
}

void HDControl::disableHD(){
	hdCommand.hd_disable();
	return;
}

/**
 * Set the volume.  We take a scale of 0-100, but it is internally converted
 * to a scale of 0-90, which is what the radio accepts.  That means some
 * values, such as 9 and 10, 19 and 20, and so on (each ending in 9 and the next
 * one) will result in the same level.  (This is because integer division is
 * used in the conversion.)
 * @param newlevel the level to set the volume to
 */
void HDControl::hd_setvolume(int newlevel) {//public
	hdCommand.hd_setitem("volume", newlevel, 90);
	return;
}

/**
 * Set the volume.  We take a scale of 0-100, but it is internally converted
 * to a scale of 0-90, which is what the radio accepts.  That means some
 * values, such as 9 and 10, 19 and 20, and so on (each ending in 9 and the next
 * one) will result in the same level.  (This is because integer division is
 * used in the conversion.)
 * @param newlevel the level to set the volume to (will be converted to an int)
 */
void HDControl::hd_setvolume(string newlevel) {//public
	int i = atoi(newlevel.c_str());
	hd_setvolume(i);
	return;
}

/**
 * Set the bass.  We take a scale of 0-100, but it is internally converted
 * to a scale of 0-90, which is what the radio accepts.  That means some
 * values, such as 9 and 10, 19 and 20, and so on (each ending in 9 and the next
 * one) will result in the same level.  (This is because integer division is
 * used in the conversion.)
 * @param newlevel the level to set the bass to
 */
void HDControl::hd_setbass(int newlevel) {//public
	hdCommand.hd_setitem("bass", newlevel, 90);
	return;
}

/**
 * Set the bass.  We take a scale of 0-100, but it is internally converted
 * to a scale of 0-90, which is what the radio accepts.  That means some
 * values, such as 9 and 10, 19 and 20, and so on (each ending in 9 and the next
 * one) will result in the same level.  (This is because integer division is
 * used in the conversion.)
 * @param newlevel the level to set the bass to (will be converted to an int)
 */
void HDControl::hd_setbass(string newlevel) {//public
	int inum = atoi(newlevel.c_str());
	hd_setbass(inum);
	return;
}

/**
 * Set the treble.  We take a scale of 0-100, but it is internally converted
 * to a scale of 0-90, which is what the radio accepts.  That means some
 * values, such as 9 and 10, 19 and 20, and so on (each ending in 9 and the next
 * one) will result in the same level.  (This is because integer division is
 * used in the conversion.)
 * @param newlevel the level to set the treble to
 */
void HDControl::hd_settreble(int newlevel) {//public
	hdCommand.hd_setitem("treble", newlevel, 90);
	return;
}

/**
 * Set the treble.  We take a scale of 0-100, but it is internally converted
 * to a scale of 0-90, which is what the radio accepts.  That means some
 * values, such as 9 and 10, 19 and 20, and so on (each ending in 9 and the next
 * one) will result in the same level.  (This is because integer division is
 * used in the conversion.)
 * @param newlevel the level to set the treble to (will be converted to an int)
 */
void HDControl::hd_settreble(string newlevel) {//public
	int inum = atoi(newlevel.c_str());
	hd_settreble(inum);
	return;
}

/**
 * Tune directly to the default station and subchannel.  By default this is set
 * to WCVE-FM (classical and jazz) in Richmond, VA, where the original program
 * author lives.  The values can be changed in the config files.  The main purpose
 * was to make it easier to tune to a station while testing and debugging.
 */
void HDControl::tunetodefault() {//public
	hdCommand.hd_tune(deffreq, defband);
	return;
}

/**
 * Tune one unit up.  If there are hdsubchannels, it'll turn up to the next
 * subchannel, otherwise it'll turn up to the next frequency.
 */
void HDControl::hd_tuneup() {//public
	hdCommand.hd_tuneupdown("up");
	return;
}

/**
 * Tune one unit down.  If there are hdsubchannels, it'll turn down to the next
 * subchannel, otherwise it'll turn down to the next frequency.
 */
void HDControl::hd_tunedown() {//public
	hdCommand.hd_tuneupdown("down");
	return;
}

/**
 * Specify an HD subchannel.  If there is no HD streamlock or if that subchannel
 * does not exist or can't be locked on to, nothing will happen.
 * @param newchannel channel to tune to in string form
 */
void HDControl::hd_subchannel(int newchannel) {//public
	hdCommand.hd_setitem("hdsubchannel", newchannel, 0);
	return;
}

/**
 * Specify an HD subchannel.  If there is no HD streamlock or if that subchannel
 * does not exist or can't be locked on to, nothing will happen.
 * @param newchannel channel to tune to
 */
void HDControl::hd_subchannel(string newchannel) {//public
	int i = atoi(newchannel.c_str());
	hd_subchannel(i);
	return;
}

/**
 * Seek up the dial for the next subchannel or frequency.
 */
void HDControl::hd_seekup() {//public
	hdCommand.hd_seekupdown("up");
	return;
}

/**
 * Seek down the dial for the next subchannel or frequency.
 */
void HDControl::hd_seekdown() {//public
	hdCommand.hd_seekupdown("down");
	return;
}

/**
 * Specify to seek all stations when seeking (the default).  Any station
 * with a strong enough signal will be sought.
 */
void HDControl::hd_seekall() {//public
	hdCommand.hd_seekall();
	return;
}

/**
 * Specify to seek only HD stations when seeking.  (Not the default.)
 */
void HDControl::hd_seekhd() {//public
	hdCommand.hd_seekhd();
	return;
}

/**
 * Ask the radio to send us its power setting.
 */
void HDControl::request_power() {//public
	hdCommand.hdget("power");
	return;
}

/**
 * Ask the radio to send us its volume setting.
 */
void HDControl::request_volume() {//public
	hdCommand.hdget("volume");
	return;
}

/**
 * Ask the radio to send us its mute setting.
 */
void HDControl::request_mute() {//public
	hdCommand.hdget("mute");
	return;
}

/**
 * Ask the radio to send us its bass setting.
 */
void HDControl::request_bass() {//public
	hdCommand.hdget("bass");
	return;
}

/**
 * Ask the radio to send us its treble setting.
 */
void HDControl::request_treble() {//public
	hdCommand.hdget("treble");
	return;
}

/**
 * Ask the radio to send us its current tuner setting.
 */
void HDControl::request_tune() {//public
	hdCommand.hdget("tune");
	return;
}

/**
 * Ask the radio to send us the subchannel count for this station.
 */
void HDControl::request_hdsubchannelcount() {//public
	hdCommand.hdget("hdsubchannelcount");
	return;
}

/**
 * Ask the radio to send us the current subchannel.
 */
void HDControl::request_hdsubchannel() {//public
	hdCommand.hdget("hdsubchannel");
	return;
}

/**
 * Ask the radio to send us the current station name
 */
void HDControl::request_hdstationname() {//public
	hdCommand.hdget("hdstationname");
	return;
}

/**
 * Ask the radio to send us the current station call sign
 */
void HDControl::request_hdcallsign() {//public
	hdCommand.hdget("hdcallsign");
	return;
}

/**
 * Ask the radio to send us its HD unique ID.
 */
void HDControl::request_hduniqueid() {//public
	hdCommand.hdget("hduniqueid");
	return;
}

/**
 * Ask the radio to send us the current HD title info
 */
void HDControl::request_hdtitle() {//public
	hdCommand.hdget("hdtitle");
	return;
}

/**
 * Ask the radio to send us the current HD artist info
 */
void HDControl::request_hdartist() {//public
	hdCommand.hdget("hdartist");
	return;
}

/**
 * Ask the radio to send us the strength of the current HD signal
 */
void HDControl::request_hdsignalstrenth() {//public
	hdCommand.hdget("signalstrength");
	return;
}

/**
 * Ask the radio to send us the status of the HD stream lock
 */
void HDControl::request_hdstreamlock() {//public
	hdCommand.hdget("streamlock");
	return;
}

/**
 * Ask the radio to send us whether HD is active on this station or not
 */
void HDControl::request_hdactive() {//public
	hdCommand.hdget("hdactive");
	return;
}

/**
 * Ask the radio to send us whether the HD tuner is enabled or not
 */
void HDControl::request_hdtunerenabled() {//public
	hdCommand.hdget("hdenablehdtuner");
	return;
}

/**
 * Ask the radio to send us its API version.
 */
void HDControl::request_apiversion() {//public
	hdCommand.hdget("hdapiversion");
	return;
}

/**
 * Ask the radio to send us its hardware version.
 */
void HDControl::request_hwversion() {//public
	hdCommand.hdget("hdhwversion");
	return;
}

/**
 * Ask the radio to send us whether RDS is enabled on this station
 */
void HDControl::request_rdsenable() {//public
	hdCommand.hdget("rdsenable");
	return;
}

/**
 * Ask the radio to send us the RDS programming service for this station
 */
void HDControl::request_rdsservice() {//public
	hdCommand.hdget("rdsservice");
	return;
}

/**
 * Ask the radio to send us the current RDS text for this station
 */
void HDControl::request_rdstext() {//public
	hdCommand.hdget("rdstext");
	return;
}

/**
 * Ask the radio to send us the RDS genre for this station.
 */
void HDControl::request_rdsgenre() {//public
	hdCommand.hdget("rdsgenre");
	return;
}

/**
 */
void HDControl::showdtr() {//public
	bool dtrstate;
	dtrstate = ioPort.getdtr();
	cout << "DTR State: " << dtrstate << endl;
	return;
}

void HDControl::setDTR(bool on){
	ioPort.toggledtr(on);
}

/**
 * Mainly for debugging: switch the DTR to a specific state
 * @param arg set to true/1/high to turn DTR on, anything else for off
 */
void HDControl::toggledtr(string arg) {//public
	bool newstate = false;
	if (arg == "true" || arg == "1" || arg == "high")
		newstate = true;
	ioPort.toggledtr(newstate);
	return;
}

/**
 * Normally we do hang up, or turn off the radio, on exit, but in
 * some systems it may be desireable to leave the radio on on exit, so
 * that can be done by calling here to override the default with "false".
 * @param hangup true to turn off the radio on exit, false to leave it on
 */
void HDControl::hanguponexit(bool hangup) {//public
	ioPort.hanguponexit(hangup);
	return;
}

//----------------------------------------------------------
//Get values from listener
//----------------------------------------------------------

/**
 * Get a specified value from OUR current variables.  This does not ask the
 * radio to send us a value, it asks for our current value of the specified variable.
 * @param valname name of variable/value wanted
 * @return value asked for
 */
string HDControl::getValue(string valname) {//public
	string val = hdListen.gethdvalue(valname);
	return val;
}

/**
 * Get the power status from our current settings list.
 * @return the current setting for the power state (true or false)
 */
string HDControl::getPower() {//public
	string val = hdListen.gethdvalue("power");
	return val;
}

/**
 * Get the volume status from our current settings list. The
 * radio uses a scale of 0-90 but we conver to a scale of
 * 0-100.
 * @return the current setting for the volume
 */
string HDControl::getVolume() {//public
	string val = hdListen.gethdvalue("volume");
	return val;
}

/**
 * Get the bass status from our current settings list. The
 * radio uses a scale of 0-90 but we conver to a scale of
 * 0-100.
 * @return the current setting for the bass level
 */
string HDControl::getBass() {//public
	string val = hdListen.gethdvalue("bass");
	return val;
}

/**
 * Get the treble status from our current settings list.  The
 * radio uses a scale of 0-90 but we conver to a scale of
 * 0-100.
 * @return the current setting for the treble level
 */
string HDControl::getTreble() {//public
	string val = hdListen.gethdvalue("treble");
	return val;
}

/**
 * Get the mute status from our current settings list.
 * @return the current setting for the mute (true or false)
 */
string HDControl::getMute() {//public
	string val = hdListen.gethdvalue("mute");
	return val;
}

/**
 * Get the frequency status from our current settings list.  (Only the
 * frequency, not the band or a subchannel.)  This is returned as a string,
 * but it's an int in style since the decimal point on any FM frequencies is
 * removed.
 * @return the current setting for frequency
 */
string HDControl::getFrequency() {//public
	string val = hdListen.gethdvalue("frequency");
	return val;
}

/**
 * Get the band status from our current settings list.
 * @return the current setting for band (am or fm)
 */
string HDControl::getBand() {//public
	string val = hdListen.gethdvalue("band");
	return val;
}

/**
 * Get the subchannel status from our current settings list.
 * @return the current setting for the current hdsubchannel
 */
string HDControl::getHDSubchannel() {//public
	string val = hdListen.gethdvalue("hdsubchannel");
	return val;
}

/**
 * Get the tune setting from our current settings list.  It'll be in
 * a format like "88.9:1 FM".  This string could also be given to tune()
 * to tune to a station.  The first 3 digits (and decimal point) are the
 * frequency, the digit after the colon (could be 2, but not by today's standards)
 * is the hdsubchannel and the last 2 characters are the band.
 * @return the current setting for where the radio is tuned.
 */
string HDControl::getTune() {//public
	string val = hdListen.gethdvalue("tune");
	return val;
}

/**
 * Get the ative status for the hd signal from our current settings list.
 * @return the current setting for if the hd signal is active (true or false)
 */
string HDControl::getHDActive() {//public
	string val = hdListen.gethdvalue("hdactive");
	return val;
}

/**
 * Get the streamlock status from our current settings list.  If this is
 * true, we have a lock on the hdsubchannel, if not, we don't.
 * @return the current setting for the hdstreamlock (true or false)
 */
string HDControl::getHDStreamlock() {//public
	string val = hdListen.gethdvalue("hdstreamlock");
	return val;
}

/**
 * Get the strength of the hd signal from our current settings list.
 * @return the strength of the hd signal.
 */
string HDControl::getHDSignalStrength() {//public
	string val = hdListen.gethdvalue("hdsignalstrength");
	return val;
}

/**
 * Get the number of subchannels available for the current frequency.
 * @return the subchannel count (in string, not int format)
 */
string HDControl::getHDSubchannelCount() {//public
	string val = hdListen.gethdvalue("subchannelcount");
	return val;
}

/**
 * Get if the tuner is currently hd enabled.  It's unclear if this
 * means the tuner can get an hd signal or is receiving it.
 * @return true if it is enabled
 */
string HDControl::getHDEnableTuner() {//public
	string val = hdListen.gethdvalue("hdenabletuner");
	return val;
}

/**
 * Get the call sign as provided by hd data from the current station.
 * @return the call sign for this station
 */
string HDControl::getHDCallSign() {//public
	string val = hdListen.gethdvalue("hdcallsign");
	return val;
}

/**
 * Get the station name as provided by hd data from the current station.
 * @return the station name
 */
string HDControl::getHDStationName() {//public
	string val = hdListen.gethdvalue("hdstationname");
	return val;
}

/**
 * Get the HD unique ID.  It is unclear if this is a radio model number,
 * a serial number, or something else.
 * @return the HD Unique ID
 */
string HDControl::getHDUniqueID() {//public
	string val = hdListen.gethdvalue("hduniqueid");
	return val;
}

/**
 * Get the API version of the radio.
 * @return version of the API
 */
string HDControl::getHDAPIVersion() {//public
	string val = hdListen.gethdvalue("hdapiversion");
	return val;
}

/**
 * Get the hardware version for this radio.
 * @return version of the hardware used
 */
string HDControl::getHDHWVersion() {//public
	string val = hdListen.gethdvalue("hdhwversion");
	return val;
}

/**
 * Get info on whether the RDS is available for this channel.
 * @return false if RDS is not available here
 */
string HDControl::getRDSEnable() {//public
	string val = hdListen.gethdvalue("rdsenable");
	return val;
}

/**
 * Get the RDS genre string describing the current station from our
 * settings.  It is reset with each tune change so it is either an
 * empty string or current.  RDS data is not as reliable as HD data
 * and can often drop characters or have incorrect characters.
 * @return RDS text describing the current station's genre
 */
string HDControl::getRDSGenre() {//public
	string val = hdListen.gethdvalue("rdsgenre");
	return val;
}

/**
 * Get the RDS programming service string describing the current station from our
 * settings.  It is reset with each tune change so it is either an
 * empty string or current.  RDS data is not as reliable as HD data
 * and can often drop characters or have incorrect characters.
 * @return RDS text describing the current station's programming service
 */
string HDControl::getRDSProgramService() {//public
	string val = hdListen.gethdvalue("rdsprogramservice");
	return val;
}

/**
 * Get the RDS radio text describing the current station from our
 * settings.  It is reset with each tune change so it is either an
 * empty string or current.  RDS data is not as reliable as HD data
 * and can often drop characters or have incorrect characters.
 * @return RDS text describing the current station
 */
string HDControl::getRDSRadioText() {//public
	string val = hdListen.gethdvalue("rdsradiotext");
	return val;
}

/**
 * Get the title on the current hd channel.
 * @return the specific title
 */
string HDControl::getHDTitle() {//public
	string val = hdListen.gethdvalue("hdtitle");
	return val;
}

/**
 * Get the title on a specific hd channel.
 * @param channel the channel to get the title name for
 * @return the specific title
 */
string HDControl::getHDTitle(int channel) {//public
	return hdListen.gethdtitle(channel);
}

/**
 * The test radio returned the artist and title for all the hdsubchannels on
 * a particular frequency, not just the current one.  This returns a list of
 * all the titles on all the subchannels;
 * @return a map of all the titles, with the channel number being keyed to each title
 */
map<int,string> HDControl::getHDTitles() {//public
	return hdListen.gethdtitles();
}

/**
 * Get the artist on the current hd channel.
 * @return the specific artist
 */
string HDControl::getHDArtist() {//public
	string val = hdListen.gethdvalue("hdartist");
	return val;
}

/**
 * Get the artist on a specific hd channel.
 * @param channel to get the artist name for
 * @return the specific artist
 */
string HDControl::getHDArtist(int channel) {//public
	return hdListen.gethdtitle(channel);
}

/**
 * The test radio returned the artist and title for all the hdsubchannels on
 * a particular frequency, not just the current one.  This returns a list of
 * all the artists on all the subchannels;
 * @return a map of all the artists, with the channel number being keyed to each title
 */
map<int,string> HDControl::getHDArtists() {//public
	return hdListen.gethdartists();
}
