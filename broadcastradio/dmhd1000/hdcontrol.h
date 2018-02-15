#ifndef MYFILE_HDCONTROL
#define MYFILE_HDCONTROL

#include <string>
#include <jni.h>
#include "hdlinuxio.h"
#include "hdcommands.h"
#include "hdlisten.h"
#include "hddefs.h"

using namespace std;

class HDControl {
		 bool verbose;
		 string deffreq, defband;
		 string mainconfigfile, sessionconfigfile;
		 string defaultserial;
		int defstreamwait;
		string defvol;
		string defbass;
		string deftreb;
		 LinuxPort ioPort;
		 HDListen hdListen;
		 HDVals hdValues;
		 HDCommands hdCommand;

	public:
		HDControl();
		static const int BAND_AM = 1;
		static const int BAND_FM = 2;
		void setArguments(int, char**);
		void setSerialPort(string);
		void activate();
		void passCB(
			android::hardware::broadcastradio::V1_1::ProgramSelector,
			android::hardware::broadcastradio::V1_1::ProgramInfo,
			android::sp<android::hardware::broadcastradio::V1_1::ITunerCallback>
		);
//		void passJvm(JavaVM *, jclass);
		void close();
		void setVerbose(bool);
		void restoreState();
		bool command_line(string);
		bool command(string);
		bool command(string, string);
		bool command(string, int);
		bool command(string, string, string);
		bool command(string, int, string);
		bool tune(int, int, int);
		void radioOn();
		void radioOff();
		void muteOn();
		void muteOff();
		void disableHD();
		void hd_setvolume(int);
		void hd_setvolume(string);
		void hd_setbass(int);
		void hd_setbass(string);
		void hd_settreble(int);
		void hd_settreble(string);
		void tunetodefault();
		void hd_tuneup();
		void hd_tunedown();
		void hd_subchannel(int);
		void hd_subchannel(string);
		void hd_seekup();
		void hd_seekdown();
		void hd_seekall();
		void hd_seekhd();
		void request_power();
		void request_volume();
		void request_mute();
		void request_bass();
		void request_treble();
		void request_tune();
		void request_hdsubchannelcount();
		void request_hdsubchannel();
		void request_hdstationname();
		void request_hdcallsign();
		void request_hduniqueid();
		void request_hdtitle();
		void request_hdartist();
		void request_hdsignalstrenth();
		void request_hdstreamlock();
		void request_hdactive();
		void request_hdtunerenabled();
		void request_apiversion();
		void request_hwversion();
		void request_rdsenable();
		void request_rdsservice();
		void request_rdstext();
		void request_rdsgenre();
		void showdtr();
		void setDTR(bool);
		void toggledtr(string);
		void hanguponexit(bool);
		string getValue(string);
		string getPower();
		string getVolume();
		string getBass();
		string getTreble();
		string getMute();
		string getFrequency();
		string getBand();
		string getHDSubchannel();
		string getTune();
		string getHDActive();
		string getHDStreamlock();
		string getHDSignalStrength();
		string getHDSubchannelCount();
		string getHDEnableTuner();
		string getHDCallSign();
		string getHDStationName();
		string getHDUniqueID();
		string getHDAPIVersion();
		string getHDHWVersion();
		string getRDSEnable();
		string getRDSGenre();
		string getRDSProgramService();
		string getRDSRadioText();
		string getHDTitle();
		string getHDTitle(int);
		map<int,string> getHDTitles();
		string getHDArtist();
		string getHDArtist(int);
		map<int,string> getHDArtists();

	protected:

	private:
		string intToString(int);

};

#endif
