/*
TMRh20 2014

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

/** General Data Transfer Rate Test
 * This example demonstrates basic data transfer functionality with the
 updated library. This example will display the transfer rates acheived using
 the slower form of high-speed transfer using blocking-writes.
 */


#include <cstdlib>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <string>
#include <RF24/RF24.h>
#include <unistd.h>
#include <fstream>
#include <time>

using namespace std;

#define DATA_WORKING_DIRECTORY 	"/home/pi/node_data"

#define MS_WAIT_BETWEEN_PACKETS 60000
#define SUPPORTED_NUM_NODES		64

#define discover_packet_size	16
#define provision_packet_size   14
#define node_info_packet_size   5
#define node_data_packet_size   18
#define gateway_ack_packet_size 6


enum packet_types
{
	null_packet_type = 0,
	discover_packet_type = 1,
	provision_packet_type = 2,
	node_info_packet_type = 3,
	node_data_packet_type = 4,
	gateway_ack_packet_type = 5
};

struct discover_packet
{
	uint8_t packet_type;
	char message[15];
};

struct provision_packet
{
	uint8_t packet_type;
	uint8_t node_id;
	uint32_t address;
	uint32_t mswait;
};

struct node_info_packet
{
	uint8_t packet_type;
	uint8_t VBUS_available;
	uint8_t bmp280_available;
	uint8_t si7020_available;
	uint8_t hardware_revision[2];
};

struct node_data_packet
{
	uint8_t packet_type;
	uint8_t node_id;
	uint32_t BMP280_pres;
	int32_t BMP280_temp;
	uint32_t Si7020_humid;
	int32_t Si7020_temp;
};

struct gateway_ack_packet
{
	uint8_t packet_type;
	uint8_t node_id;
	uint32_t mswait;
};

struct all_packets
{
	discover_packet discover;	
	provision_packet provision;
	node_info_packet node_info;
	node_data_packet node_data;
	gateway_ack_packet ack;
};

all_packets temp_packets;
//
// Hardware configuration
//

/****************** Raspberry Pi ***********************/

// Radio CE Pin, CSN Pin, SPI Speed
// See http://www.airspayce.com/mikem/bcm2835/group__constants.html#ga63c029bd6500167152db4e57736d0939 and the related enumerations for pin information.

// Setup for GPIO 22 CE and CE0 CSN with SPI Speed @ 4Mhz
//RF24 radio(RPI_V2_GPIO_P1_22, BCM2835_SPI_CS0, BCM2835_SPI_SPEED_4MHZ);

// NEW: Setup for RPi B+
//RF24 radio(RPI_BPLUS_GPIO_J8_15,RPI_BPLUS_GPIO_J8_24, BCM2835_SPI_SPEED_8MHZ);

// Setup for GPIO 15 CE and CE0 CSN with SPI Speed @ 8Mhz
//RF24 radio(RPI_V2_GPIO_P1_15, RPI_V2_GPIO_P1_24, BCM2835_SPI_SPEED_8MHZ);

/*** RPi Alternate ***/
//Note: Specify SPI BUS 0 or 1 instead of CS pin number.
// See http://tmrh20.github.io/RF24/RPi.html for more information on usage

//RPi Alternate, with MRAA
//RF24 radio(15,0);

//RPi Alternate, with SPIDEV - Note: Edit RF24/arch/BBB/spi.cpp and  set 'this->device = "/dev/spidev0.0";;' or as listed in /dev
RF24 radio(22,0);


/****************** Linux (BBB,x86,etc) ***********************/

// See http://tmrh20.github.io/RF24/pages.html for more information on usage
// See http://iotdk.intel.com/docs/master/mraa/ for more information on MRAA
// See https://www.kernel.org/doc/Documentation/spi/spidev for more information on SPIDEV

// Setup for ARM(Linux) devices like BBB using spidev (default is "/dev/spidev1.0" )
//RF24 radio(115,0);

//BBB Alternate, with mraa
// CE pin = (Header P9, Pin 13) = 59 = 13 + 46 
//Note: Specify SPI BUS 0 or 1 instead of CS pin number. 
//RF24 radio(59,0);

/**************************************************************/

// Radio pipe addresses for the 2 nodes to communicate.
const uint32_t gateway_address 	= {		0xdf569390};
const uint32_t addresses[] 		= {		0x0a4c9afb,
										0x039d2552,
										0x765e5196,
										0x501e2f44,
										0x7548fac9,
										0x5da0cab1,
										0x77c4d117,
										0x1fd5a00a};
const uint32_t default_node_address = {	0xac04b1a4};

uint8_t next_expected_node = 0;
bool any_node_available = 0;
uint64_t next_expected_node_millis;
bool nodes_available[SUPPORTED_NUM_NODES];
uint64_t node_next_millis[SUPPORTED_NUM_NODES];
uint8_t failures[SUPPORTED_NUM_NODES];
uint8_t packet[32];
unsigned long startTime, stopTime, counter, rxTimer=0;


void update_wait_time(void)
{
	next_expected_node_millis = millis() + 59990;
	next_expected_node = 0;
	any_node_available = 0;
	for(int i = 0; i < SUPPORTED_NUM_NODES; i++)
	{
		if(nodes_available[i])
		{
			any_node_available = 1;

			if(node_next_millis[i] < next_expected_node_millis)
			{
				next_expected_node = i;
				next_expected_node_millis = node_next_millis[i];
			}
		}
	}
}

uint32_t calculateWaitTime(int node_id)
{
	uint32_t timeslice = (MS_WAIT_BETWEEN_PACKETS) * node_id / SUPPORTED_NUM_NODES;
	int64_t wait_time =  MS_WAIT_BETWEEN_PACKETS + timeslice - (millis() % MS_WAIT_BETWEEN_PACKETS);
	cout << "Wait time: " << wait_time << endl;
	return wait_time;
}

FILE *file1;

void get_node_info(int node)
{
	string temp_string = DATA_WORKING_DIRECTORY;
	temp_string += "/node";
	temp_string += to_string(node);
	temp_string += "/info";
	file1 = fopen(temp_string.c_str(), "rb");
	if(file1)
	{
		fread(&temp_packets.node_info, 1, node_info_packet_size, file1);
		fclose(file1);
		file1 = NULL;
	}
}

void post_node_info(int node)
{
	string temp_string = DATA_WORKING_DIRECTORY;
	temp_string += "/node";
	temp_string += to_string(node); 
	temp_string += "/info";
	file1 = fopen(temp_string.c_str(), "wb");
	fwrite(&temp_packets.node_info, 1, node_info_packet_size, file1);
	fclose(file1);
	file1 = NULL;
	temp_string += ".txt";
	ofstream output_file(temp_string.c_str());
	output_file << "VBUS_available: " << ((temp_packets.node_info.VBUS_available == 0) ? "False" : "True") << endl;
	output_file << "bmp280_available: " << ((temp_packets.node_info.bmp280_available == 0) ? "False" : "True") << endl;
	output_file << "si7020_available: " << ((temp_packets.node_info.si7020_available == 0) ? "False" : "True") << endl;
	output_file << "hardware_revision: " << temp_packets.node_info.hardware_revision[0] << temp_packets.node_info.hardware_revision[1] << endl;
	output_file.close();
}

void post_node_data(void)
{
	string temp_string = DATA_WORKING_DIRECTORY;
	temp_string += "/node" ;
	temp_string += to_string(temp_packets.node_data.node_id); 
	temp_string += "/data/";
	ofstream output_file(temp_string.c_str());
	get_node_info(temp_packets.node_data.node_id);
	if(temp_packets.node_info.bmp280_available)
	{
		output_file << "BMP280 Temp: " << temp_packets.node_data.BMP280_temp << endl;
		output_file << "BMP280 Pres: " << temp_packets.node_data.BMP280_pres << endl;
	}
	if(temp_packets.node_info.si7020_available)
	{
		output_file << "Si7020 Temp: " << temp_packets.node_data.Si7020_temp << endl;
		output_file << "Si7020 Humi: " << temp_packets.node_data.Si7020_humid << endl;
	}
	output_file.close();
}

void add_node(int node)
{
	string temp_string = "mkdir ";
	temp_string += DATA_WORKING_DIRECTORY;
	temp_string += "/node";
	temp_string += to_string(node);
	system(temp_string.c_str());
}

void remove_node(int node)
{
	string temp_string = "rm ";
	temp_string += DATA_WORKING_DIRECTORY;
	temp_string += "/node";
	temp_string += to_string(node);
	temp_string += " -r";
	system(temp_string.c_str());
}

int main(int argc, char** argv)
{
	// Do some quick initilization of the global variables
	temp_packets.ack.packet_type = gateway_ack_packet_type;
	temp_packets.provision.packet_type = provision_packet_type;
	temp_packets.node_info.packet_type = node_info_packet_type;
	temp_packets.node_data.packet_type = node_data_packet_type;
	temp_packets.discover.packet_type = discover_packet_type;

	for(uint i = 0; i < SUPPORTED_NUM_NODES; i++)
	{
		nodes_available[i] = false;
	}

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

	char listening_message[] = "I'm here!\n\0";
	bool pass = true;
	uint64_t tempMillis;
	uint8_t next_node_id = 0;
    // forever loop
	while (1)
	{
		while(radio.available())
		{
			cout << "Message Received: ";
			radio.read(&packet, 32);
			for(uint i = 0; i < 32; i++)
			{
				cout << (packet[i]);
			}
			cout << ("\n");
			switch((packet_types) packet[0])
			{
				case discover_packet_type:
					cout << "Discover packet type detected" << endl;
					// Copy the contents of the packet to the temp packet. Too bad there isn't a way to efficiently do this. Yet.
					for(uint i = 1; i < discover_packet_size; i++)
					{
						temp_packets.discover.message[i-1] = packet[i];
					}

					// Compare the message we recieved with the listening message
					for(uint i = 0; i < sizeof(listening_message); i++)
					{
						cout << temp_packets.discover.message[i];
						if(temp_packets.discover.message[i] != listening_message[i])
						{
							i = 100;
							pass = false;
						}
					}

					// If the messages are the same, then reply with the expected reply.
					if(pass)
					{
						char password_msg[] = "I hear you";
						for(uint i = 0; i < sizeof(password_msg); i++)
						{
							temp_packets.discover.message[i] = password_msg[i];
						}
						delay(5);
						radio.stopListening();
						radio.write(&temp_packets.discover, discover_packet_size);
						radio.startListening();
					}


					// We'll break out if the messages weren't the same.
					else 
					{
						cout << "Didn't receive the right message" << endl;
						break;
					}

					delay(5);

					// I'm surprised I can declare a variable here. Last time it threw up in my general direction.
					

					// Find the next available node ID and we'll do stuff from there
					for(uint i = 0; i < SUPPORTED_NUM_NODES; i++)
					{
						if(!nodes_available[i])
						{
							next_node_id = i;
							i = SUPPORTED_NUM_NODES;
							cout << "Next node is: " << next_node_id << "\n";
						}
					}
					nodes_available[next_node_id] = true;

					// Now we need to... send out a provision packet
					temp_packets.provision.node_id = next_node_id;
					temp_packets.provision.address = addresses[next_node_id % sizeof(addresses)/sizeof(addresses[0])];
					temp_packets.provision.mswait = calculateWaitTime(next_node_id);
					cout << "Node ID: " << to_string(temp_packets.provision.node_id) << endl;
					cout << "address: " << to_string(temp_packets.provision.address) << endl;
					cout << "mswait: " << to_string(temp_packets.provision.mswait) << endl;

					radio.stopListening();
					radio.write(&temp_packets.provision, provision_packet_size);
					radio.openWritingPipe(1, temp_packets.provision.address);
					radio.startListening();
					node_next_millis[next_node_id] = temp_packets.provision.mswait;

					tempMillis = millis();
					while((millis() - tempMillis) < 10000)
					{
						if(radio.available())
						{
							radio.read(&temp_packets.node_info, node_info_packet_size);
							break;
						}
					}

					if((millis() - tempMillis) >= 10000)
					{
						cout << "Didn't get the node info stuff" << endl;
						break;
					}
					
					add_node(next_node_id);
					post_node_info(next_node_id);
					update_wait_time();
				break;

				// These two packets are only sent by the gateway, so we shouldn't see them unless there's another gateway.
				case provision_packet_type:
					cout << "Provision packet type detected" << endl;
				break;
				case gateway_ack_packet_type:
					cout << "Ack packet type detected" << endl;
				break;

				// These two shouldn't be transmitted to the default gateway address. If they are, we either want to correct them or
				// Allow them to reset by ignoring them.
				case node_data_packet_type:
					cout << "Data packet type detected" << endl;
				break;
				case node_info_packet_type:
					cout << "Info packet type detected" << endl;
				break;	
				default:
				cout << "Packet type: " << packet[0]  << "\n";
				break;
			}
			radio.stopListening();
			radio.openReadingPipe(1, default_node_address);
			radio.startListening();
		}
	} // loop

	return 0;
} // main
