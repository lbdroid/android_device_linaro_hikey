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
#include <map>
#include <iostream>
// #include "hdcommands.h"
#include "hddefs.h"
using namespace std;

//Leave these comments in place -- the original author uses them with a
//Perl script to generate headers

//HDVals::		volatile bool keepreading;

//HDVals::		map<string,string> hd_cmds;
//HDVals::		map<string,string> hd_codes;
//HDVals::		map<string,string> hd_format;
//HDVals::		map<string,string> hd_ops;
//HDVals::		map<string,string> hd_band;
//HDVals::		map<string,string> hd_constants;
//HDVals::		map<string,string> hd_scale;

/**
 * Constructor for HDVals.  Set all the variables.  All the data used by the various
 * classes HDControl needs is stored here, such as command names and codes, as well as
 * replies and any constants used in reading from or writing to the radio.  We keep the values
 * and provide the different pieces of data through a get() interface.
 */
HDVals::HDVals() {//public
	keepreading = true;

	hd_cmds["power"] = "0x01 0x00";
	hd_cmds["mute"] = "0x02 0x00";

	hd_cmds["signalstrength"] = "0x01 0x01";
	hd_cmds["tune"] = "0x02 0x01";
	hd_cmds["seek"] = "0x03 0x01";

	hd_cmds["hdactive"] = "0x01 0x02";
	hd_cmds["hdstreamlock"] = "0x02 0x02";
	hd_cmds["hdsignalstrength"] = "0x03 0x02";
	hd_cmds["hdsubchannel"] = "0x04 0x02";
	hd_cmds["hdsubchannelcount"] = "0x05 0x02";
	hd_cmds["hdenablehdtuner"] = "0x06 0x02";
	hd_cmds["hdtitle"] = "0x07 0x02";
	hd_cmds["hdartist"] = "0x08 0x02";
	hd_cmds["hdcallsign"] = "0x09 0x02";
	hd_cmds["hdstationname"] = "0x10 0x02";
	hd_cmds["hduniqueid"] = "0x11 0x02";
	hd_cmds["hdapiversion"] = "0x12 0x02";
	hd_cmds["hdhwversion"] = "0x12 0x02";

	hd_cmds["rdsenable"] = "0x01 0x03";
//Place holders -- reminder that there may be a few more commands
//we haven't yet documented.
//	hd_cmds[""] = "0x02 0x03";
//	hd_cmds[""] = "0x03 0x03";
//	hd_cmds[""] = "0x04 0x03";
//	hd_cmds[""] = "0x05 0x03";
//	hd_cmds[""] = "0x06 0x03";
	hd_cmds["rdsgenre"] = "0x07 0x03";
	hd_cmds["rdsprogramservice"] = "0x08 0x03";
	hd_cmds["rdsradiotext"] = "0x09 0x03";

//	hd_cmds[""] = "0x01 0x04";
//	hd_cmds[""] = "0x02 0x04";
	hd_cmds["volume"] = "0x03 0x04";
	hd_cmds["bass"] = "0x05 0x04";
	hd_cmds["treble"] = "0x05 0x04";
	hd_cmds["compression"] = "0x06 0x04";

	hd_ops["set"] = "0x00 0x00";
	hd_ops["get"] = "0x01 0x00";
	hd_ops["reply"] = "0x02 0x00";

	hd_band["am"] = "0x00 0x00 0x00 0x00";
	hd_band["fm"] = "0x01 0x00 0x00 0x00";

	hd_constants["up"] = "0x01 0x00 0x00 0x00";
	hd_constants["down"] = "0xFF 0xFF 0xFF 0xFF";
	hd_constants["one"] = "0x01 0x00 0x00 0x00";
	hd_constants["zero"] = "0x00 0x00 0x00 0x00";
	hd_constants["begincommand"] = "0xA4";

//Types of returned data:
//string, int: tuning
//int: volume, bass, treble, signalstrength, subchannel, subchannelcount
//boolean: power, mute, hdactive, streamlock, enablehdtuner (?)
//string: title, artist, callsign, stationname, uniqueid, apiversion, hwversion, rdsgenre, rdsservice, rdstext

	hd_format["power"] = "boolean";
	hd_format["mute"] = "boolean";

	hd_format["signalstrength"] = "int";
	hd_format["tune"] = "band:int";
	hd_format["seek"] = "band:int";

	hd_format["hdactive"] = "boolean";
	hd_format["hdstreamlock"] = "boolean";
	hd_format["hdsignalstrength"] = "int";
	hd_format["hdsubchannel"] = "int";
	hd_format["hdsubchannelcount"] = "int";
	hd_format["hdenablehdtuner"] = "boolean";
	hd_format["hdtitle"] = "int:string";
	hd_format["hdartist"] = "int:string";
	hd_format["hdcallsign"] = "string";
	hd_format["hdstationname"] = "string";
	hd_format["hduniqueid"] = "string";
	hd_format["hdapiversion"] = "string";
	hd_format["hdhwversion"] = "string";

	hd_format["rdsenable"] = "boolean";
//	hd_format[""] = "";
//	hd_format[""] = "";
//	hd_format[""] = "";
//	hd_format[""] = "";
//	hd_format[""] = "";
	hd_format["rdsgenre"] = "string";
	hd_format["rdsprogramservice"] = "string";
	hd_format["rdsradiotext"] = "string";

//	hd_format[""] = "";
//	hd_format[""] = "";
	hd_format["volume"] = "int";
	hd_format["bass"] = "int";
	hd_format["treble"] = "int";
	hd_format["compression"] = "";

	hd_scale["volume"] = "true";
	hd_scale["bass"] = "true";
	hd_scale["treble"] = "true";

	map<string,string>::iterator it;
	for (it = hd_cmds.begin(); it != hd_cmds.end(); it++) {
		hd_codes[(*it).second] = (*it).first;
	}
	return;
}

/**
 * Get the hex codes for a command to send to the radio.  Call with the
 * command, get a string of hex codes to send out.
 * @param command command to get hex codes for
 * @return the hex codes to send to the radio
 */
string HDVals::getcode(string command) {//public
	string val = hd_cmds[command];
	return val;
}

/**
 * Get the command or the name for a reply to match a hex sequence
 * the radio sent as a response.
 * @param code string form of hex codes in a format like "0xA4 0x1B"
 * @return the reply name or command (they're the same) that matches the code
 */
string HDVals::getcommand(string code) {//public
	string val = hd_codes[code];
	return val;
}

/**
 * Get the hext code for any type of operation the radio takes.
 * @param name the name of the type of operation (like set, get or reply)
 * @return the hex codes, in string form, of the corresponding bytes.
 */
string HDVals::getop(string name) {//public
	string val = hd_ops[name];
	return val;
}

/**
 * Get the hex bytes form that the radio uses to represent a band, like am or fm.
 * @param name the name (am or fm) of the band
 * @return the value, in hex string bytes, that the radio uses for that band
 */
string HDVals::getband(string name) {//public
	string val = hd_band[name];
	return val;
}

/**
 * Get a constant value, such as up, down, zero, and so on.  Give the name
 * and get the hex bytes that correspond.
 * @param name name of the constant needed
 * @return the hex bytes in string form that the radio uses for this constant
 */
string HDVals::getconstant(string name) {//public
	string val = hd_constants[name];
	return val;
}

/**
 * Get the format for a string of data.  When data comes in as a response from
 * the radio, some is just boolean, some will be int, some int and string.  This
 * tells what type of data a specific command/reply is.
 * @param command the command or response name to check
 * @return the type of data format
 */
string HDVals::getformat(string command) {//public
	string val = hd_format[command];
	return val;
}

/**
 * Get whether or not a value is scaled.  Some values, such as the volume level,
 * are based on a scale of 0-90 instead of a more human scale of 0-100.  Call here
 * with the name of the variable the radio recognizes to find out if it works on a
 * scaled down range.
 * @param name the name of the value, such as volume, treble, bass
 * @return true if it's on a scale other than 0-100
 */
bool HDVals::getscaled(string name) {//public
	bool isscaled = false;
	string val = hd_scale[name];
	if (val == "true")
		isscaled = true;
	return isscaled;
}

