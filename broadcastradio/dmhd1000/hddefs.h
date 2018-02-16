#ifndef MYFILE_HDDEFS
#define MYFILE_HDDEFS

#include <string>
#include <map>
#include <iostream>

using namespace std;

class HDVals {
		 volatile bool keepreading;
		 map<string,string> hd_cmds;
		 map<string,string> hd_codes;
		 map<string,string> hd_format;
		 map<string,string> hd_ops;
		 map<string,string> hd_band;
		 map<string,string> hd_constants;
		 map<string,string> hd_scale;

		string commands[32][32];

	public:
		HDVals();
		string getcmd(unsigned char, unsigned char);
		string getcode(string);
		string getcommand(string);
		string getop(string);
		string getband(string);
		string getconstant(string);
		string getformat(string);
		bool getscaled(string);

	protected:

	private:

};



#endif
