#ifndef MYFILE_HDLINUXIO
#define MYFILE_HDLINUXIO

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>

using namespace std;

class LinuxPort {
		 bool isOpen, verbose;
		 int portfd, lastread, cflag;
		 float /*autodiscoverwait,*/ defautodiscwait;
		 unsigned int testparm[11];
		 unsigned long naptime;
		 long BAUD;
		 long DATABITS;
		 long STOPBITS;
		 long PARITYON;
		 long PARITY;
		 string serialdevice;
		 string testdata;
		 struct termios options/*, oldios*/;

	public:
		LinuxPort();
		void setverbose(bool);
		bool testport(string);
		void setportattr(int);
		bool openport();
		void closeport();
		void setserialport(string);
		string getserialport();
		void hdsendbyte(char);
		void hdsendbytes(char*);
		unsigned char* hdreadbytes(int);
		char* hdreadbyte();
		int hdlastreadleangth();
		void toggledtr(bool);
		void printdtr();
		void printdtr(string);
		bool getdtr();
		void hanguponexit(bool);

	protected:
		void chout(unsigned char);

	private:

};



#endif
