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
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <stdio.h>
#include "hdcontrol.h"
#include "hdcommands.h"
#include "hdlisten.h"
#include "hdcmd.h"
using namespace std;


	vector<string> defkeys;
	HDControl hdc;

/**
 * Display the name of a variable and its value.
 * @param valkey the key of the variable we're going to display
 */
void printval(string valkey) {
	string val = hdc.getValue(valkey);
	cout << "\tKey: " << valkey << ", Value: " << val << endl;
	return;

}

/**
 * Parse a command typed in or given on the command line.
 * @param cmdline a command with arguments separated by spaces to be parsed.
 */
void parsecmd(string cmdline) {
	bool getcmd = true;
	int ispace;
	unsigned int i, total, inap;
	string cmd, arg, title, artist;
	map<int,string> hdtitles, hdartists;

	ispace = cmdline.find_first_of(" ");
	if (ispace < 0) {
		cmd = cmdline;
		arg = "";
	} else {
		cmd = cmdline.substr(0, ispace);
		arg = cmdline.substr(ispace + 1, cmdline.size());
	}
	if (cmd == "exit") {
		cout << "Exiting program...\n";
		hdc.close();
		exit(0);
	} else if (cmd == "show") {
		if (arg == "all" || arg == "") {
			arg = hdc.getValue("hdsubchannelcount");
			if (arg == "") arg = "0";
			sscanf(arg.c_str(), "%d", &total);
			hdtitles = hdc.getHDTitles();
			hdartists = hdc.getHDArtists();
			if (total > 10) total = 10;
			for (i = 1; i <= total; i++) {
				title = hdtitles[i];
				artist = hdartists[i];
				cout << "\tSubchannel: " << i << ", Title: " << title << ", Artist: " << artist << endl;
			}
		} else if (arg == "def" || arg == "default") {
			for (i = 0; i < defkeys.size(); i++) {
				printval(defkeys[i]);
			}
		} else {
			printval(arg);
		}
	} else if (cmd == "sleep") {
		sscanf(arg.c_str(), "%d", &inap);
		cout << "Sleeping for " << inap << " seconds...\n";
		sleep(inap);
	} else {
		getcmd = hdc.command_line(cmdline);
		if (!getcmd)
			cout << "Command not recognized or wrong number of arguments: " << cmdline << endl;
	}
	return;
}

/**
 * Get commands from the keyboard and process them.  We handle "exit" and "show" and
 * everything else is passed on to the HD Radio Controller for processing.  Exit will
 * leave the program (and shut the radio off).  Show will display variable values.  If
 * no arguments are given, all variables currently stored are displayed.  If "def" is given
 * a set of default variables is displayed instead of all.  If the name of a variable is
 * given, that variable's value is displayed.
 */
void getcommands() {
	string cmdline;
	while (true) {
		cout << "Command: ";
		getline(cin, cmdline);
// 		cout << "\tCommand Entered: " << cmdline << endl;
		parsecmd(cmdline);
	}
}

/**
 * This is a command line interface to the HDControl object for controlling
 * HD radios from Linux through a serial port.  It's mainly for testing so to
 * get a command list, you'll have to look at commands that we parse in
 * parsecmd() and in the command() functions in HDControl (note there are
 * 3 different ones, divided up by the number of arguments).
 *
 * It is also possible to pass commands from the command line.  A "," as an
 * argument by itself is the default command separator so commands of multiple
 * argumengs can be passed with spaces between args.
 *
 * Pass on the command line arguments to the config file for parsing.
 * Also get commands back from config file and pass them on to the
 * HDRadio control object for processing.  Then set up to get commands
 * from the console.
 */
int main(const int argc, char* argv[]) {
	unsigned int i;
	string cmd, cmdline, sep = ",";
	vector<string> cmds;

	defkeys.push_back("tune");
	defkeys.push_back("callsign");
	defkeys.push_back("station");
	defkeys.push_back("subchannel");
	defkeys.push_back("hdtitle");
	defkeys.push_back("hdartist");
	defkeys.push_back("rdsgenre");
	defkeys.push_back("rdsprogramservice");
	defkeys.push_back("rdsradiotext");

	hdc.activate();
	hdc.setVerbose(true);
	cmd = "";
	for (i = 0; i < cmds.size(); i++) {
// 		cout << "Queue: " << i << ", Command: " << cmds[i] << endl;
		if (cmds[i] == sep) {
			cout << "Executing Command: " << cmd << endl;
		parsecmd(cmd);
			cmd = "";
			continue;
		}
		if (cmd.size() > 0)
			cmd += " ";
		cmd += cmds[i];

	}
	if (cmd != "") {
		cout << "Executing Command: " << cmd << endl;
		parsecmd(cmd);
	}
parsecmd("volume 100");
parsecmd("tune 895 fm");
	getcommands();

	return 0;
}
