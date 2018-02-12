#ifndef MYFILE_HDLISTEN
#define MYFILE_HDLISTEN

#include <iostream>
#include <string>
#include <map>
#include <pthread.h>
#include <jni.h>
#include "hddefs.h"
#include "hdlinuxio.h"

using namespace std;

class HDListen {
		 bool verbose;
		 bool keepReading, havecode, valueset, escChar, lengthWait;
		 char msgcode[16], msgtype[16], curmsg[1024];
		 int msglen, msgin, lastsubchannelcount;
		 //currentsubchannel,lastsubchannel,
		 unsigned int cktotal;
		 unsigned long naptime;
		 string ctype, currentmsg, currfreq, currband, lasttune;
		int* bq;
		 map<string,string> radiovals;
		 map<int,string> hdtitles;
		 map<int,string> hdartists;
		 pthread_t listenThread;
		 pthread_mutex_t valLock, trackLock;
		 //time_t changetimer;
		 HDVals* hdvals;
		 LinuxPort* ioport;
//		 JavaVM * s_Jvm;
//		 jclass j_cls;

	public:
		HDListen();
		void setverbose(bool);
		void setdefs(HDVals*);
		void setioport(LinuxPort*);
//		void passJvm(JavaVM *, jclass);
		void listenthread();
		void listentoradio();
		string gethdvalue(string);
		int gethdintval(string);
		bool gethdboolval(string);
		string gethdtitle(int);
		map<int,string> gethdtitles();
		string gethdartist(int);
		map<int,string> gethdartists();
		void stopreading();

	protected:
		void sethdval(string, string);
		void chout(unsigned char);
		int hexbytestoint(string);
		string hexbytestostring(string);
		string decodemsg();
		void procmsg();
		void handlebyte(unsigned char);
		void readinfile();

	private:
		void sethdtitle(int, string);
		void sethdartist(int, string);
		void callback(string,string);

};


void *StartHDListener(void*);

#endif
