#ifndef MYFILE_HDCOMMANDS
#define MYFILE_HDCOMMANDS

#include <list>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <termios.h>
#include "hddefs.h"
#include "hdlinuxio.h"
#include "hdlisten.h"

using namespace std;

class HDCommands {
		 bool seekall, verbose;
		 int streamlockwait;
		 LinuxPort* ioport;
		 HDListen* hdlisten;
		 HDVals* hdvals;

	public:
		HDCommands();
		void setdefs(HDVals*);
		void setioport(LinuxPort*);
		void setlisten(HDListen*);
		void setverbose(bool);
		void setstreamwait(int);
		void hd_power(string);
		void hd_mute(string);
		void hd_disable();
		void hd_setitem(string, int, int);
		bool hd_tune(string);
		bool hd_tune(string, string);
		bool hd_tune(int, string);
		bool hd_tune(string, string, string);
		bool hd_tune(int, int, int);
		bool hd_tune(int, int, string);
		void hd_tuneupdown(string);
		void hd_seekupdown(string);
		void hdget(string);
		void hd_seekall();
		void hd_seekhd();

	protected:

	private:
		void sendcommand(list<string>);
		string removedecimal(string);
		string getnum(string, int);

};



#endif
