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
#include <sstream>
#include <string>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include "hdlinuxio.h"
#include <android/log.h>
using namespace std;

#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"RADIO",__VA_ARGS__)

/**
 * Create the LinuxPort class.  Set all initial variables.
 */
LinuxPort::LinuxPort() {
	isOpen = false;
	portfd = 0; lastread = 0; cflag = 0xA4;
	testparm[0] = 0xA4; testparm[1] = 0x08;  testparm[2] = 0x01;  testparm[3] = 0x00;
	testparm[4] = 0x02; testparm[5] = 0x00;  testparm[6] = 0x01;  testparm[7] = 0x00;
	testparm[8] = 0x00;  testparm[9] = 0x00; testparm[10] = 0xB0;
//DEBUG: larger number makes debugging easier, smaller makes it respond faster
	naptime = 100;
	BAUD = B115200;
	DATABITS = CS8;
	STOPBITS = 0;
	PARITYON = 0;
	PARITY = 0;
	serialdevice = ""; testdata = "";
	verbose = false;
	defautodiscwait = 10;
//DEBUG:
// 	verbose = true;
}

/**
 * Set the verbosity.  Provides output to the console for debugging.
 * @param verbosity true for console output
 */
void LinuxPort::setverbose(bool verbosity) {//public
	verbose = verbosity;
	return;
}

/**
 * Test a port to see if a radio we can work with is connected.  We open it
 * with openport() and wait to see if that turned on a radio that
 * sends us a power reply.  Checking a port could disrupt communications with
 * any other device on that port, so this is not recommended for use each time
 * a program is run but only the first time, to find the radio.
 * @param serport the serial port to check
 */
bool LinuxPort::testport(string serport) { //public
	unsigned char cIn;
	char* buff = new char[5];
	int i = 0, rd;
	double itime;
	float adjwait;		//Wait up to this many seconds for signal
	bool result, doreply = false;
	time_t tstart, tnow;

	cout << "Testing port for HD Radio Control: " << serport << endl;
	if (serport == "") {
		return false;
	}
	adjwait = 1;
	if (verbose) cout << "Wait time set: " << adjwait;
	setserialport(serport);
	result = openport();
	if (!result) {
		cout << "Error opening port: " << serport << endl;
		cout << "Cannot verify HD Radio on port: " << serport << endl;
		closeport();
		return false;
	}
	fcntl(portfd, F_SETFL, O_NONBLOCK);
	time(&tstart);
	while (true) {
		rd = read(portfd, buff, 1);
		time(&tnow);
		itime = difftime(tnow, tstart);
// 		if (verbose) cout << "\tWait time, in seconds: " << itime << endl;
		if (itime >= adjwait) {
			cout << "Response time too long.  Port failed: " << serport << endl;
			cout << "Cannot verify HD Radio on port: " << serport << endl;
			closeport();
			return false;
		}
		usleep(naptime);
		if (rd < 1) continue;
		cIn = buff[0];
		chout(cIn);
		adjwait += .5;
		if (verbose) cout << "Got bytes from port!, New Wait time: " << adjwait << endl;
		if (doreply) {
			if (verbose) cout << "\tComparing to parm #: " << i << endl;
			if (cIn != testparm[i]) {
// 				cout << "Bad byte in sequence, count: " << i << ", Byte: " << testparm[i] << endl;
				cout << "Cannot verify HD Radio on port: " << serport << endl;
				closeport();
				return false;
			}
			i++;
// 			adjwait += .5;
// 			cout << "\tCurrent count: " << i << ", Size: " << sizeof(testparm)/sizeof(i) << endl;
			if (i == sizeof(testparm)/sizeof(int)) {
				cout << "Port matched for HD Radio: " << serport << endl;
				fcntl(portfd, F_SETFL, 0);
				return true;
			}

		} else if (cIn == cflag) {
			if (verbose) cout << "We have the first command byte\n";
			doreply = true;
			i++;
		}
	}
	cout << "Cannot verify HD Radio on port: " << serport << endl;
	closeport();
	return false;
}

/**
 * Set the serial port attributes so we can communicate with the radio.  This
 * could effect the state of the DTR line in some cases.
 * @param portfd the file descriptor for the port we're setting
 */
void LinuxPort::setportattr(int fd) { //public
	struct termios tty;
	int status;
	memset (&tty, 0, sizeof tty);
	if (tcgetattr (fd, &tty) != 0){
		printf ("error %d from tcgetattr\n", errno);
		return;
	}

	cfsetospeed (&tty, B115200);
	cfsetispeed (&tty, B115200);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;	// 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break as \000 chars
	tty.c_iflag &= ~IGNBRK;				// disable break processing
	tty.c_lflag = 0;				// no signaling chars, no echo,
							// no canonical processing
	tty.c_oflag = 0;				// no remapping, no delays
	tty.c_cc[VMIN]  = 0xff;				// read blocks for min 255 bytes
	tty.c_cc[VTIME] = 5;				// 0.5 seconds read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY);		// shut off xon/xoff ctrl

	tty.c_cflag |= (CLOCAL | CREAD);		// ignore modem controls,
							// enable reading
	tty.c_cflag &= ~(PARENB | PARODD);		// shut off parity
	tty.c_cflag |= 0;//parity;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if (tcsetattr (fd, TCSANOW, &tty) != 0){
		printf ("error %d from tcsetattr\n", errno);
		return;
	}
	ioctl(portfd, TIOCMGET, &status);
	status &= ~TIOCM_RTS;
	ioctl(portfd, TIOCMSET, &status);
	return;
}

/**
 * We opne the serial port here.  The port should already be specified with setserialport().
 */
bool LinuxPort::openport() { //public
	if (isOpen) return true;
	isOpen = true;
	cout << "Opening port: " << serialdevice << endl;
	portfd = open(serialdevice.c_str(), O_RDWR | O_NOCTTY | O_SYNC);// | O_NDELAY);
	setportattr(portfd);
	if (portfd == 0) {
		return false;
	} else {
		cout << "\tPort " << serialdevice << " has been opened.  Descriptor: " << portfd << endl;
	}
	return true;
}

/**
 * Close the serial port for both input and output (the serial port is opened twice, once
 * for input and once for output.
 */
void LinuxPort::closeport() { //public
	isOpen = false;
	if (portfd == 0)
		return;
	if (verbose) printdtr("About to close ports");
	close(portfd);
	cout << "Serial port closed: " << serialdevice << endl;
	return;
}

/**
 * Set the serial port to use for communication.  When it's set here, it's also
 * set in the config so it'll be written out to the config file as the last known
 * port.  During autodiscovery that won't help, but the config file isn't saved during
 * autodiscovery.  It is saved whenever settings on the radio change, so setting the
 * serial port in the config class here will ensure the port is saved when the radio
 * is tuned to any station.
 *
 * On startup next time, the port saved will be used again unless overridden.
 * @param sport serial port
 */
void LinuxPort::setserialport(string sport) { //public
	serialdevice = sport;
	if (verbose) cout << "Setting serial device: " << serialdevice << endl;
	return;
}

/**
 * Get the currently used serial port file name.
 * @return the path or name of the currently used serial device
 */
string LinuxPort::getserialport() { //public
	return serialdevice;
}

/**
 * Send a byte to the radio through the serial port.  Actually just calls hdsendbytes() and says
 * to send only 1 byte.
 * @param outbyte character or byte to send through the port
 */
void LinuxPort::hdsendbyte(char outbyte) { //public
	write(portfd, &outbyte, 1);
// 	cout << "Sending byte...\n";
// 	int rc = write(portfd, &outbyte, 1);
// 	cout << "Send bytes, FD: " << portfd << ", Return code: " << rc << endl;
	return;
}

/**
 * Send a series of bytes or characters through the serial port to the device.
 * @param outbytes the characters/bytes to send
 */
void LinuxPort::hdsendbytes(char* outbytes) { //public
	write(portfd, outbytes, sizeof(outbytes));
// 	int rc = write(portfd, outbytes, sizeof(outbytes));
// 	cout << "Send bytes, FD: " << portfd << ", Return code: " << rc << endl;
	return;
}

/**
 * Read the specified number of bytes from the serial port.  Our buffer is 1k, since
 * we're never working with anywhere near that much data anyway.
 * @param ilen number of bytes to read in
 * @return pointer to the buffer where  returned data is stored.
 */
unsigned char* LinuxPort::hdreadbytes(int ilen) { //public
	unsigned char* buff = new unsigned char[1024];
	int rd = 0;

	int n, i;
	unsigned char cs;
	buff[0] = 0;
	if (ilen > 1024)
		return buff;
	if (!isOpen) {
		LOGD("Having to re-open port");
		openport();
	}
	while (rd < 1) {
		n = read(portfd, buff, 1);
		if (n != 1 || buff[0] != 0xa4) continue;
		n = read(portfd, buff+1, 1);
		if (n != 1) continue;
		n = read(portfd, buff+2, buff[1]+1);
		if (n < buff[1]+1) continue;
		cs = 0;
		for (i=0; i<buff[1]+2; i++){
			cs += buff[i];
		}
		LOGD("Calculated CS: 0x%02X, read CS: 0x%02X", cs, buff[buff[1]+2]);
		if (cs != buff[buff[1]+2]) continue;

		rd = 1;
	}
	lastread = rd;
	return buff;
}

/**
 * Read a single byte into a buffer and return it.
 * @return pointer to a 1 byte buffer with one byte read from the port
 */
char* LinuxPort::hdreadbyte() { //public
	char* buff = (char*)hdreadbytes(1);
	return buff;
}

/**
 * Number of bytes read in on the last read.
 * @return number of bytes read in
 */
int LinuxPort::hdlastreadleangth() { //public
	return lastread;
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
void LinuxPort::chout(unsigned char cIn) {//protected

	char chex [10];
	int iIn;

	iIn = cIn;
	if (iIn == 0xA4) {
		cout << endl << endl;
	}
	sprintf(chex, "0x%2X", cIn);
	if (chex[2] == ' ')
		chex[2] = '0';
	cout << chex << ":" << iIn << " ";
// 	cout << endl;
	return;
}

/**
 * Another debugging routine.  Call with true to turn DTR on, false to turn it off.
 * @param newstate to set DTR to
 */
void LinuxPort::toggledtr(bool newstate) {//public
	int status;
	if (verbose) printdtr("---->Before toggling DTR");
	ioctl(portfd, TIOCMGET, &status);
	if (newstate) {
		cout << "Setting dtr\n";
		status |=  TIOCM_DTR;
	} else {
		cout << "Clearing dtr\n";
		status &= ~TIOCM_DTR;
	}
	ioctl(portfd, TIOCMSET, &status);
	if (verbose) printdtr("---->After toggling DTR");

	return;
}

/**
 * Overload with no argument for debugging DTR issues.
 */
void LinuxPort::printdtr() {//public
	printdtr("");
	return;
}

/**
 * Mainly for debugging, this will print out the state of the DTR line.
 */
void LinuxPort::printdtr(string msg) {//public
	bool dtrstate;
	dtrstate = getdtr();
// 	int status;
// 	fcntl(portfd, F_SETFL, 0);
// 	ioctl(portfd, TIOCMGET, &status);
	cout << msg << ", DTR State: " << dtrstate << endl;
	return;
}

/**
 * Mainly for debugging, get DTR state.
 * @return true for high, false for low.
 */
bool LinuxPort::getdtr() {//public
	bool result = false;
	int status;
// 	fcntl(portfd, F_SETFL, 0);
	ioctl(portfd, TIOCMGET, &status);
	if ((status & TIOCM_DTR) == 1)
		result = true;
	return result;
}

/**
 * Normally we do hang up, or turn off the radio, on exit, but in
 * some systems it may be desireable to leave the radio on on exit, so
 * that can be done by calling here to override the default with "false".
 * @param hangup true to turn off the radio on exit, false to leave it on
 */
void LinuxPort::hanguponexit(bool hangup) {//public
	tcgetattr(portfd, &options);
	if (hangup)
		options.c_cflag |= HUPCL;
	else
		options.c_cflag &= ~HUPCL;
	tcsetattr(portfd, TCSANOW, &options);
	return;
}

