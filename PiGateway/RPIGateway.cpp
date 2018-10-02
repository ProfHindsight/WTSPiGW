/*
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */


#include <cstdlib>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <string>
#include <RF24/RF24.h>
#include <unistd.h>
#include <fstream>
#include <time.h>

using namespace std;

#define DATA_WORKING_FILE 	"/home/pi/node_data/datafile.csv"
#define DATA_WORKING_DIRECTORY "/home/pi/node_data"

#define minimalist_packet_size	31


enum packet_types
{
	minimalist_packet_type = 6
};

typedef struct minimalist_packet
{
	uint8_t packet_type;
	uint8_t VBUS_available;
	uint8_t bmp280_available;
	uint8_t si7020_available;
	uint8_t hardware_revision[2];
	uint32_t unique_id_0;
	uint32_t unique_id_1;
	uint32_t BMP280_pres;
	int32_t BMP280_temp;
	uint32_t Si7020_humid;
	int32_t Si7020_temp;
}minimalist_struct_t, *p_minimalist_struct_t;

p_minimalist_struct_t mpGlobalPacket;


//RPi Alternate, with SPIDEV - Note: Edit RF24/arch/BBB/spi.cpp and  set 'this->device = "/dev/spidev0.0";;' or as listed in /dev
RF24 radio(22,0);


// Radio pipe addresses for the 2 nodes to communicate.
const uint32_t gateway_address 	= {		0xdf569390};
const uint32_t default_node_address = {	0xac04b1a4};

uint8_t packet[32];

FILE *file1;

bool create_file(void)
{
	file1 = fopen(DATA_WORKING_FILE, "w");
	if (file1 == NULL) {
		// Probably didn't make the directory. Or making the file for the first time.
    	string temp_string = "mkdir ";
		temp_string += DATA_WORKING_DIRECTORY;
		system(temp_string.c_str());
		cout << "Making directory" << endl;

		// Try again
		file1 = fopen(DATA_WORKING_FILE, "w");
		if(file1 == NULL)
		{
			return false;
		}
		cout << "Directory Created: " << DATA_WORKING_DIRECTORY << endl;
    }
    fprintf(file1, "%s\n", "Timestamp, Unique ID, VBUS Available, BMP280 Available, Si7020 Available, Hardware Revision, BMP280 Pressure (Pa), BMP280 Temperature (C), Si7020 Humidity (%), Si7020 Temperature (C)");
    fclose(file1);  
    return true;
}

bool post_minimalist_node(minimalist_packet* mpt)
{
	file1 = fopen(DATA_WORKING_FILE, "r");
	if(file1 == NULL)
	{
		create_file();
	}
	else
	{
		fclose(file1);
	}

	file1 = fopen(DATA_WORKING_FILE, "a");
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[40];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%m/%d/%Y %X", &tstruct);
    // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
    // for more information about date/time format
	fprintf(file1, "%s,",buf);
	fprintf(file1, "%X%X,", mpt->unique_id_1, mpt->unique_id_0);
	fprintf(file1, "%d,%d,%d,", 
			mpt->VBUS_available,
			mpt->bmp280_available,
			mpt->si7020_available);
	fprintf(file1, "%c.%c,", mpt->hardware_revision[0], mpt->hardware_revision[1]);
	fprintf(file1, "%d,%0.2f,%0.3f,%0.3f\n", 
			mpt->BMP280_pres,
			((float) mpt->BMP280_temp) / 100,
			((float) mpt->Si7020_humid) / 1000,
			((float) mpt->Si7020_temp) / 1000);

	fclose(file1);
	return true;
}

int main(int argc, char** argv)
{

	// Print preamble:
  	cout << "Starting the gateway thing\n";
  	radio.begin();                           // Setup and configure rf radio
	
	cout << "Started the gateway fine \n";
	radio.setChannel(1);
	radio.setPALevel(RF24_PA_MAX);	
	radio.setAddressWidth(4);
	radio.setDataRate(RF24_1MBPS);
	radio.setAutoAck(1);                     // Ensure autoACK is enabled
	radio.setRetries(15,15);                  // Optionally, increase the delay between retries & # of retries
	radio.setCRCLength(RF24_CRC_8);          // Use 8-bit CRC for performance
	radio.stopListening();
	radio.printDetails();
	radio.openWritingPipe(default_node_address);
	radio.openReadingPipe(1,gateway_address);
	radio.startListening();
	cout << "Setup radio fine \n";

	
    // forever loop
	while (1)
	{

		while(radio.available())
		{
			cout << "Message Received: ";
			radio.read(&packet, 32);
			for(uint i = 0; i < 32; i++)
			{
				cout << to_string(packet[i]);
			}
			cout << endl;
			switch((packet_types) packet[0])
			{
				// Only handling one packet type for now. You can see the old code for handling other packet types.

				case minimalist_packet_type:
					cout << "Minimalist packet type detected" << endl;
					uint8_t temp_array[minimalist_packet_size];
					memcpy(temp_array, packet, minimalist_packet_size);
					mpGlobalPacket = (p_minimalist_struct_t) temp_array;
					if(post_minimalist_node(mpGlobalPacket) == true)
					{
						cout << "Posted Data" << endl;
					}
					else
					{
						cout << "Failed to post data" << endl;
					}
				break;
				default:
				cout << "Packet type: " << packet[0]  << "\n";
				break;
			}
		}
		delay(1);
	} // loop

	return 0;
} // main





