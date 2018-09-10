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

#define DATA_WORKING_DIRECTORY 	"/home/pi/node_data/normal_nodes"
#define DATA_MINIMALIST_DIRECTORY "/home/pi/node_data/minimalist_nodes"

#define MS_WAIT_BETWEEN_PACKETS 6000
#define SUPPORTED_NUM_NODES		64

#define discover_packet_size	32
#define provision_packet_size   14
#define node_info_packet_size   14
#define node_data_packet_size   18
#define gateway_ack_packet_size 6
#define minimalist_packet_size	31


enum packet_types
{
	discover_packet_type = 1,
	provision_packet_type = 2,
	node_info_packet_type = 3,
	node_data_packet_type = 4,
	gateway_ack_packet_type = 5,
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


struct discover_packet
{
	uint8_t packet_type;
	char message[31];
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
	uint32_t unique_id_0;
	uint32_t unique_id_1;
};

typedef struct node_data_packet
{
	uint8_t packet_type;
	uint8_t node_id;
	uint32_t BMP280_pres;
	int32_t BMP280_temp;
	uint32_t Si7020_humid;
	int32_t Si7020_temp;
} node_data_struct_t, *p_node_data_struct_t;

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
	p_node_data_struct_t node_data;
	gateway_ack_packet ack;
	p_minimalist_struct_t mp;
};

all_packets temp_packets;

//RPi Alternate, with SPIDEV - Note: Edit RF24/arch/BBB/spi.cpp and  set 'this->device = "/dev/spidev0.0";;' or as listed in /dev
RF24 radio(22,0);


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
const uint8_t address_array_size = 8;
const uint32_t default_node_address = {	0xac04b1a4};

uint8_t timeout_node = 0;
bool any_node_available = 0;
uint64_t timeout_minimum_millis;
bool nodes_available[SUPPORTED_NUM_NODES];
uint64_t timeout_node_millis[SUPPORTED_NUM_NODES];
uint8_t failures[SUPPORTED_NUM_NODES];
uint8_t packet[32];
unsigned long startTime, stopTime, counter, rxTimer=0;


void update_wait_time(void)
{
	timeout_minimum_millis = millis() + MS_WAIT_BETWEEN_PACKETS * 8;
	timeout_node = 0;
	any_node_available = 0;
	for(int i = 0; i < SUPPORTED_NUM_NODES; i++)
	{
		if(nodes_available[i])
		{
			any_node_available = 1;

			if(timeout_node_millis[i] < timeout_minimum_millis)
			{
				timeout_node = i;
				timeout_minimum_millis = timeout_node_millis[i];
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

bool post_node_info(int node)
{
	string temp_string = DATA_WORKING_DIRECTORY;
	temp_string += "/node";
	temp_string += to_string(node); 
	file1 = fopen(temp_string.c_str(), "r");
	if(file1 == NULL)
	{
		return false;
	}
	fclose(file1);
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
	char buffer[20];
	sprintf(buffer, "%X%X", temp_packets.node_info.unique_id_0, temp_packets.node_info.unique_id_1);
	output_file << "Unique ID: 0x" << buffer << endl;
	cout << "Unique ID: 0x" << buffer << endl;
	output_file.close();
	return true;
}

bool post_node_data(void)
{
	string temp_string = DATA_WORKING_DIRECTORY;
	temp_string += "/node" ;
	temp_string += to_string(temp_packets.node_data->node_id); 
	file1 = fopen(temp_string.c_str(), "r");
	if (file1 == NULL) {
        return false;
    }
    fclose(file1);  
	temp_string += "/data.txt"; 
	ofstream output_file(temp_string.c_str());
	get_node_info(temp_packets.node_data->node_id);
	output_file << "BMP280 Temp: " << temp_packets.node_data->BMP280_temp << endl;
	output_file << "BMP280 Pres: " << temp_packets.node_data->BMP280_pres << endl;
	output_file << "Si7020 Temp: " << temp_packets.node_data->Si7020_temp << endl;
	output_file << "Si7020 Humi: " << temp_packets.node_data->Si7020_humid << endl;
	output_file.close();
	return true;
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

void remove_all_nodes(void)
{
	string temp_string = "rm ";
	temp_string += DATA_WORKING_DIRECTORY;
	temp_string += "/node*";
	temp_string += " -r";
	system(temp_string.c_str());
}

void add_directories(void)
{
	string temp_string = "mkdir ";
	temp_string += DATA_WORKING_DIRECTORY;
	temp_string += " ";
	temp_string += DATA_MINIMALIST_DIRECTORY;
	system(temp_string.c_str());
}


bool post_minimalist_node(minimalist_packet* mpt)
{
	string temp_string = DATA_MINIMALIST_DIRECTORY;
	temp_string += "/node";
	char buffer[20];
	sprintf(buffer, "%X%X", mpt->unique_id_0, mpt->unique_id_1);
	temp_string += buffer;

	file1 = fopen(temp_string.c_str(), "r");
	if (file1 == NULL) {
    	string temp_string = "mkdir ";
		temp_string += DATA_MINIMALIST_DIRECTORY;
		temp_string += "/node";
		temp_string += buffer;
		system(temp_string.c_str());
		cout << "Making directory" << endl;

		temp_string = DATA_MINIMALIST_DIRECTORY;
		temp_string += "/node";
		temp_string += buffer;
		file1 = fopen(temp_string.c_str(), "r");
		if(file1 == NULL)
		{
			return false;
		}
		cout << "Minimalist Directory Created: " << buffer << endl;
    }
    fclose(file1);  
	temp_string += "/data.txt"; 
	ofstream output_file(temp_string.c_str());
	output_file << "VBUS_available: " << ((mpt->VBUS_available == 0) ? "False" : "True") << endl;
	output_file << "bmp280_available: " << ((mpt->bmp280_available == 0) ? "False" : "True") << endl;
	output_file << "si7020_available: " << ((mpt->si7020_available == 0) ? "False" : "True") << endl;
	output_file << "hardware_revision: " << mpt->hardware_revision[0] << mpt->hardware_revision[1] << endl;
	output_file << "BMP280 Temp: " << mpt->BMP280_temp << endl;
	output_file << "BMP280 Pres: " << mpt->BMP280_pres << endl;
	output_file << "Si7020 Temp: " << mpt->Si7020_temp << endl;
	output_file << "Si7020 Humi: " << mpt->Si7020_humid << endl;
	output_file.close();
	return true;

}

int main(int argc, char** argv)
{
	// Do some quick initilization of the global variables
	temp_packets.ack.packet_type = gateway_ack_packet_type;
	temp_packets.provision.packet_type = provision_packet_type;
	temp_packets.node_info.packet_type = node_info_packet_type;
	temp_packets.discover.packet_type = discover_packet_type;

	for(uint i = 0; i < SUPPORTED_NUM_NODES; i++)
	{
		nodes_available[i] = false;
	}

	// Print preamble:
  	cout << "Starting the gateway thing\n";
  	add_directories();
  	remove_all_nodes();
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
	uint32_t temp_address;
    // forever loop
	while (1)
	{
		if(any_node_available)
		{
			if(millis() > timeout_minimum_millis)
			{
				remove_node(timeout_node);
				cout << "Removed node: " << to_string(timeout_node) << endl;
				nodes_available[timeout_node] = false;
				update_wait_time();
			}
		}

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
					temp_packets.provision.address = addresses[next_node_id % address_array_size];
					temp_packets.provision.mswait = calculateWaitTime(next_node_id);
					cout << "Node ID: " << to_string(temp_packets.provision.node_id) << endl;
					cout << "address: " << to_string(temp_packets.provision.address) << endl;
					cout << "mswait: " << to_string(temp_packets.provision.mswait) << endl;

					radio.stopListening();
					radio.write(&temp_packets.provision, provision_packet_size);
					radio.startListening();
					timeout_node_millis[next_node_id] = temp_packets.provision.mswait + 7 * MS_WAIT_BETWEEN_PACKETS + millis();

					tempMillis = millis();
					while((millis() - tempMillis) < 1000)
					{
						if(radio.available())
						{
							radio.read(&temp_packets.node_info, node_info_packet_size);
							break;
						}
					}

					if((millis() - tempMillis) >= 1000)
					{
						cout << "Didn't get the node info stuff" << endl;
						break;
					}
					
					add_node(next_node_id);
					post_node_info(next_node_id);
					update_wait_time();
				break;

				// These two packets are only sent by the gateway, so we shouldn't see them 
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
					uint8_t mp_temp_array[node_data_packet_size];
					memcpy(mp_temp_array, packet, node_data_packet_size);
					temp_packets.node_data = (p_node_data_struct_t) mp_temp_array;
					cout << "Node ID: " << to_string(temp_packets.node_data->node_id) << endl;
					temp_address = addresses[temp_packets.node_data->node_id % address_array_size];
					if(!post_node_data())
					{
						cout << "The node doesn't exist" << endl;
						break;
					}
					delay(25);
					radio.stopListening();
					radio.openWritingPipe(temp_address);
					cout << "Writing address: " << to_string(temp_address) << endl;
					temp_packets.ack.node_id = temp_packets.node_data->node_id;
					temp_packets.ack.mswait = calculateWaitTime(temp_packets.ack.node_id);
					timeout_node_millis[temp_packets.ack.node_id] = temp_packets.ack.mswait + 7 * MS_WAIT_BETWEEN_PACKETS + millis();
					update_wait_time();
					radio.write(&temp_packets.ack, gateway_ack_packet_size);
					radio.openWritingPipe(default_node_address);
					radio.startListening();
					cout << "I think I posted the data" << endl;
				break;

				case minimalist_packet_type:
					cout << "Minimalist packet type detected" << endl;
					uint8_t temp_array[minimalist_packet_size];
					memcpy(temp_array, packet, minimalist_packet_size);
					temp_packets.mp = (p_minimalist_struct_t) temp_array;
					if(post_minimalist_node(temp_packets.mp) == true)
					{
						cout << "Posted Data" << endl;
					}
					else
					{
						cout << "Failed to post minimalist data" << endl;
					}
				break;
				case node_info_packet_type:
					cout << "Info packet type detected" << endl;
				break;	
				default:
				cout << "Packet type: " << packet[0]  << "\n";
				break;
			}
		}
	} // loop

	return 0;
} // main
