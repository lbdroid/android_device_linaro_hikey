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

#include <list>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <stdio.h>
#include <termios.h>
#include "hddefs.h"
#include "hdlinuxio.h"
#include "hdcommands.h"
#include "hdlisten.h"

//Leave these comments in place -- the original author uses them with a
//Perl script to generate headers

//These are the info for a default station to make testing easier
//Change to whatever station is desired.  Currently it's set to a
//public radio station in Richmond, VA that subscribes to an RDS service.


//HDCommands::	bool seekall, verbose;
//HDCommands::	int streamlockwait;
//HDCommands::	LinuxPort* ioport;
//HDCommands:: HDListen* hdlisten;
//HDCommands::	HDVals* hdvals;

/**
 */
HDCommands::HDCommands() {//public
	seekall = true; verbose = false;
	streamlockwait = 10;
}

/**
 * Set the HDValue object so this class can access common variables.
 * @param hdv the HDValue object needed
 */
void HDCommands::setdefs(HDVals* hdv) {//public
	hdvals = hdv;
	return;
}

/**
 * Specify the LinuxPort object for writing to the port and the radio.
 * @param iop the port object needed to write to the serial port and radio
 */
void HDCommands::setioport(LinuxPort* iop) {//public
	ioport = iop;
	return;
}

/**
 * Provide a listener object -- needed to find out if we have streamlock in some cases.
 * @param hdl the HDListen object we need to get data
 */
void HDCommands::setlisten(HDListen* hdl) {//public
	hdlisten = hdl;
	return;
}

/**
 * Set the verbosity of this class to true if debugging statements are desired.
 * @param verbosity true for debugging output to the console
 */
void HDCommands::setverbose(bool verbosity) {//public
	verbose = verbosity;
	return;
}

/**
 * Specify how long to wait for the HDStreamlock when setting a subchannel.
 * @param wait number of seconds to wait before returning with a false when trying to set the hdsubchannel
 */
void HDCommands::setstreamwait(int wait) {//public
	streamlockwait = wait;
	return;
}

/**
	Send the actual command to the radio.  The data is passed to us
	in a list of strings.  We go through each string, add all the hex
	numbers into a long string and total their values to use in the
	checksum.  (We use them in a string because it's easier to enter
	them into maps and other info as humans and it lets us easily print
	them out for reference, as well as defining one standard way to store
	them.)
	@param inlist list of different hex strings to send to the radio
*/
void HDCommands::sendcommand(list<string> inlist) {//private
	int msgout = 0, msglen = 0;
	char byte;
	char hbyte [10];
	int dec, count = 0, csum = 0;
	string cmd = "", ref = "", line;
	list<int> iout;
	list<string>::iterator x;

	for (x = inlist.begin(); x != inlist.end(); ++x) {
		line = *x;
		istringstream iss(line);
		do {
			dec = -1;
			iss >> hex >> dec;
			if (dec < 0) {
				break;
			}
			count++;
			csum += dec;
		} while (dec >= 0);
		ref += " ";
		ref += line;
	}
	csum += count;
	msglen = count + 3;
	sprintf(hbyte, "0x%2X", count);
	if (hbyte[2] == ' ')
		hbyte[2] = '0';
	line = ref;
// 	ref = hdconstants["begincommand"];
	ref = hdvals->getconstant("begincommand");
	istringstream istr(ref);
	istr >> hex >> dec;
	csum += dec;
	csum = csum % 256;
	ref += " ";
	ref += hbyte;
	ref += line;
	sprintf(hbyte, "0x%2X", csum);
	if (hbyte[2] == ' ')
		hbyte[2] = '0';
	ref += " ";
	ref += hbyte;
	istringstream iscmd(ref);
	do {
		dec = - 1;
		iscmd >> hex >> dec;
		if (dec < 0)
			break;
// 		cout << "\tConverting to byte: " << dec << endl;
		byte = dec;
		if (dec == 0x1B || (dec == 0xA4 && (msgout > 1 && msgout < msglen - 1))) {
// 			cout << "Sending escape byte.\n";
			ioport->hdsendbyte(0x1B);
			if (dec == 0xA4) {
				dec = 0x48;
				byte = dec;
// 				cout << "Changing value to: " << dec << endl;
			}
		}
		ioport->hdsendbyte(byte);
		msgout++;
	} while (dec >= 0);;
// 	cout << "Command sent\n";
	if (verbose) cout << "\tCommand bytes: " << ref << ", Length: " << msglen << endl;
	return;
}

/**
 * Remove the decimal point from a number stored as a string.
 * @param num number in string form
 * @return the number without the decimal in it
 */
string HDCommands::removedecimal(string num) {//private
	int i;
	string simple = num;
	i = num.find('.', 0);
	if (i >= 0) {
		simple = num.substr(0, i);
		simple += num.substr(i + 1, num.size());
	}
	return simple;
}

/**
 * Get a number (in string form) from within a string starting at a specific position.
 * @param str the string with the number (and probably more data as well)
 * @param ipos the position to start the extraction at
 * @return the extracted number pulled from the string
 */
string HDCommands::getnum(string str, int ipos) {//private
	unsigned int x;
	string num = str.substr(ipos, str.size()), c;
	for (x = ipos; x < str.size(); x++) {
		c = str.substr(x, 1);
		if (str.compare(x, 1, "0") < 0 || str.compare(x, 1, "9") > 0) {
			num = str.substr(ipos, x - ipos);
			return num;
		}
	}
	return num;
}

/**
 * Turn the power on or off.
 * @param pmode Either "on" or "off" to turn the power to that state.
 */
void HDCommands::hd_power(string pmode) {//public
	list<string> cmd;
	cmd.push_back(hdvals->getcode("power"));
	cmd.push_back(hdvals->getop("set"));
	cmd.push_back(hdvals->getconstant(pmode));
	sendcommand(cmd);
	return;
}

/**
 * Turn the mute on or off.
 * @param mmode Either "on" or "off" to turn the mute to that state.
 */
void HDCommands::hd_mute(string mmode) {//public
	list<string> cmd;
	cmd.push_back(hdvals->getcode("mute"));
	cmd.push_back(hdvals->getop("set"));
	cmd.push_back(hdvals->getconstant(mmode));
	sendcommand(cmd);
	return;
}

void HDCommands::hd_disable(){
	list<string> cmd;
	cmd.push_back(hdvals->getcode("hdenablehdtuner"));
	cmd.push_back(hdvals->getop("set"));
	cmd.push_back(hdvals->getconstant("off"));
	sendcommand(cmd);
	return;
}

/**
 * Set an item in the radio to a specific value.
 * @param setname the name of the item to set
 * @param level the level (or other number) to set a value to
 * @param scale 0 if no scale is used, otherwise the top number in the scale
 */
void HDCommands::hd_setitem(string setname, int level, int scale) {//public
	char hexnum[5];
	string newlevel = "";
	list<string> cmd;

	if (verbose) cout << "Setting item: " << setname << ", Value: " << level;

	if (scale > 0) {
		level = (scale * (level + 1)) / 100;
		if (level > scale) {
			level = scale;
		}
	}
	if (verbose) cout << ", Converted value: " << level << endl;
	sprintf(hexnum, "0x%2X", level);
	if (hexnum[2] == ' ')
		hexnum[2] = '0';
	newlevel = hexnum;
	newlevel += " 0x00 0x00 0x00";
// 	cout << "Set name: " << setname << ", Bytes: " << hdcmds[setname] << endl;
	cmd.push_back(hdvals->getcode(setname));
// 	cout << "Set: " << hdops["set"] << endl;
	cmd.push_back(hdvals->getop("set"));
// 	cout << "New setting: " << newlevel << endl;
	cmd.push_back(newlevel);
	sendcommand(cmd);
	return;
}

/**
 * Overloaded version of tune that takes the same format string as saved as the "tune"
 * setting in the listener.  It's specified as the frequency, an optional colon and subchannel, a
 * space, and the band.  The decimal in the number is optional.
 * @param tuneinfo tune info in a form like "88.9:0 FM"
 * @return true if everything worked out okay
 */
bool HDCommands::hd_tune(string tuneinfo) {//public
// 	cout << "Tune, 1 arg: " << tuneinfo << endl;
	bool flag;
	int i, j;
	string freq = "", schan = "0", band = "fm";

	tuneinfo = removedecimal(tuneinfo);
	freq = getnum(tuneinfo, 0);
	i = tuneinfo.find(":");
	if (i >= 0) {
		schan = getnum(tuneinfo, i + 1);
	}
	i = tuneinfo.find("AM"); j = tuneinfo.find("am");
	if (i >= 0 || j >= 0)
		band = "am";
	flag = hd_tune(freq, schan, band);
	return flag;
}

/**
 * Overloaded version of tuen that takes just frequency and band in string format.
 * The subchannel will be defaulted to 0, which means no subchannel.
 * @param newfreq the frequency to turn to
 * @param newband the band for the new station
 * @return true if everything worked out okay
 */
bool HDCommands::hd_tune(string newfreq, string newband) {//public
// 	cout << "Tune, 2 args, strings, Freq: " << newfreq << ", Band: " << newband << endl;
	bool flag;
	int i;
	string freq = "", schan = "0";
	newfreq = removedecimal(newfreq);
	freq = getnum(newfreq, 0);
	i = newfreq.find(":");
	if (i >= 0) {
// 		cout << "Getting subchannel from string, start: " << i << ", String: " << newfreq << endl;
		schan = getnum(newfreq, i + 1);
	}
	flag = hd_tune(freq, schan, newband);
	return flag;
}

/**
 * Overloaded version of tuen that takes just frequency and band in int and string format.
 * The subchannel will be defaulted to 0, which means no subchannel.
 * @param newfreq the frequency to turn to
 * @param newband the band for the new station
 * @return true if everything worked out okay
 */
bool HDCommands::hd_tune(int newfreq, string newband) {//public
// 	cout << "Tune, 2 args, with int, Freq: " << newfreq << ", Band: " << newband << endl;
	bool flag;
	flag = hd_tune(newfreq, 0, newband);
	return flag;
}

/**
 * Overloaded version of tuen with only string arguments.  Basically we convert these
 * arguments to the forms used by the main hd_tune() function.
 * @param newfreq the frequency to turn to
 * @param newchan the new subchannel to turn to
 * @param newband the band for the new station
 * @return true if everything worked out okay
 */
bool HDCommands::hd_tune(string newfreq, string newchan, string newband) {//public
// 	cout << "Tune, 3 args, Freq: " << newfreq << ", Channel: " << newchan << ", Band: " << newband << endl;
	bool flag;
	unsigned int ifreq, ichan;
	string freq = "";
	freq = removedecimal(newfreq);
	freq = getnum(freq, 0);
	sscanf(newchan.c_str(), "%d", &ichan);
	sscanf(freq.c_str(), "%d", &ifreq);
	flag = hd_tune(ifreq, ichan, newband);
	return flag;
}

bool HDCommands::hd_tune(int freq, int channel, int band){
	if (band == 1) return hd_tune(freq, channel, "am");
	if (band == 2) return hd_tune(freq, channel, "fm");
	return false;
}

/**
 * The main tune routine to tune the radio to a frequency, band, and subchannel.
 * @param newfreq the frequency to turn to
 * @param newchannel the new subchannel to turn to
 * @param newband the band for the new station
 * @return true if everything worked (specifically setting the subchannel)
 */
bool HDCommands::hd_tune(int newfreq, int newchannel, string newband) {//public
// 	cout << "Tune, 3 args, final, Freq: " << newfreq << ", Channel: " << newchannel << ", Band: " << newband << endl;
	char chanxfer[10];
	unsigned int x;
	double itime;
	list<string> cmd;
	char xfer;
	char hexfreq [8] = "000000";
	string hex = "", oldfreq, checkchan;
	time_t tstart, tnow;

	if (newband == "FM")
		newband = "fm";
	if (newband == "AM")
		newband = "am";
	cmd.push_back(hdvals->getcode("tune"));
	cmd.push_back(hdvals->getop("set"));
	cmd.push_back(hdvals->getband(newband));
	sprintf(hexfreq, "%4X", newfreq);
//There has GOT to be a "C++" way of doing this easily: reverse the hex
//bytes and replace blanks and spaces with 0.
	for (x = 0; x < sizeof(hexfreq); x++) {
		if (hexfreq[x] == 000 || hexfreq[x] == ' ')
			hexfreq[x] = '0';
		if (x < 2) {
			xfer = hexfreq[x + 2];
			hexfreq[x + 2] = hexfreq[x];
			hexfreq[x] = xfer;
		}
		if (x % 2 == 0) {
			if (x > 0)
				hex += " ";
			hex += "0x";
		}
		hex += hexfreq[x];
	}
	cmd.push_back(hex);
	cmd.push_back(hdvals->getconstant("zero"));
	sendcommand(cmd);
	if (newchannel != 0) {
		sprintf(chanxfer, "%d", newchannel);
		checkchan = chanxfer;
		hd_setitem("hdsubchannel", newchannel, 0);
		time(&tstart);
		while (checkchan != hdlisten->gethdvalue("hdsubchannel")) {
			time(&tnow);
			itime = difftime(tnow, tstart);
			if (itime > streamlockwait) {
				if (verbose) cout << "Could not lock onto subchannel, Frequency: " << newfreq << ", Subchannel: "
						<< newchannel << ", Band: " << newband << endl;
//				Give it a try before leaving, just in case.
				hd_setitem("hdsubchannel", newchannel, 0);
				return false;
			}
			usleep(100);
			hd_setitem("hdsubchannel", newchannel, 0);
		}
// 		cout << "Subchannel set.!\n";
	}
	return true;
}

/**
 * Adjust the tuner up or down either to the next frequency or the next subchannel in
 * the specified direction.
 * @param tunedir either "up" or "down" for the direction to tune
 */
void HDCommands::hd_tuneupdown(string tunedir) {//public
	list<string> cmd;
	cmd.push_back(hdvals->getcode("tune"));
	cmd.push_back(hdvals->getop("set"));
	cmd.push_back(hdvals->getconstant("zero"));
	cmd.push_back(hdvals->getconstant("zero"));
	cmd.push_back(hdvals->getconstant(tunedir));
	sendcommand(cmd);
	return;
}

/**
 * Seek a new station.  Other functions can be used to specify whether to seek
 * only HD stations or seek all stations.
 * @param seek_dir direction ("up" or "down") to seek for a new station
 */
void HDCommands::hd_seekupdown(string seek_dir) {//public
	list<string> cmd;
	cmd.push_back(hdvals->getcode("seek"));
	cmd.push_back(hdvals->getop("set"));
	cmd.push_back("0xA5 0x00 0x00 0x00");
	cmd.push_back(hdvals->getconstant("zero"));
	cmd.push_back(hdvals->getconstant(seek_dir));
	if (seekall) {
		cmd.push_back("0x00 0x00 0x3D 0x00");
	} else {
		cmd.push_back("0x00 0x00 0x3D 0x01");
	}
	cmd.push_back(hdvals->getconstant("zero"));
	sendcommand(cmd);
	return;
}

/**
 * Send a command to the radio to return data for a particular mode or value, such as the
 * state of power or mute or the volume level or the current frequency info.
 * @param mode the mode to ask the radio to return
 */
void HDCommands::hdget(string mode) {//public
	list<string> cmd;
	cmd.push_back(hdvals->getcode(mode));
	cmd.push_back(hdvals->getop("get"));
	sendcommand(cmd);
}

/**
 * Specify to seek ALL stations when seeking.
 */
void HDCommands::hd_seekall() {//public
	seekall = true;
	return;
}

/**
 * Specify to seek ONLY HD stations and avoid stopping at non-HD stations when seeking.
 */
void HDCommands::hd_seekhd() {//public
	seekall = false;
	return;
}
