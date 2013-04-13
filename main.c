/*	
	Compilation : gcc main.c -lrt -lbluetooth -lconfig
	Author 	    : ex0ns (http://ex0ns.me)
	Started     : April 2013
*/
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>	
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <libconfig.h>


#define MAX_DEVICES 		10
#define MAX_NAME_LENGTH   	248
#define BT_ADDRESS_LENGTH	18
#define SIGNAL 			SIGRTMIN
#define WAIT_TIME		20 /* WARNING : Must be high enough so the scan could not be called twice simultaneously */

/*
	bDevice is a structure used to store the detected bluetooth devices
	The name is the human readable name of the device (hci_read_remote_name)
	Address is the bluetooth MAC address of the device
*/
typedef struct bDevice{
	char *name;
	char address[BT_ADDRESS_LENGTH];
}bDevice;
/*
	dConfig is a structure which store the configuration of a device
	This struct is filled by reading the configuration file
	startScripts contains all the scripts to load when a device is detected
	stopScripts contains all the scripts to load when a device is no longer detected
*/
typedef struct dConfig{
	struct bDevice *device;
	char **startScripts;
	char **stopScripts;
}dConfig;
/*
	Linked list, store the current detected devices, I used a global variable so I can 
	access it from everywhere, and as almost all the functions use it, it was quicker to
	do so
*/
typedef struct usedDevices{
	struct bDevice *device;
	struct usedDevices *next;
}usedDevices;

usedDevices *uDevices = NULL; 

/* 
	This function appends a device (bDevice) to the list of current connected device 
	Fill uDevices->device with a copy of param *device 
	Returns usedDevices so the global variable is always up to date !
*/
usedDevices *append(bDevice *device){
	usedDevices *newDevice = malloc(sizeof(usedDevices));
	bDevice *copy = malloc(sizeof(bDevice));
	strcpy(copy->address, device->address);
	copy->name = strdup(device->name);
	newDevice->device = copy;
	newDevice->next = NULL;
	if(uDevices == NULL){
		return newDevice;
	}
	usedDevices *temp = uDevices;
	while(temp->next != NULL){
		temp = temp->next;
	}
	temp->next = newDevice;
	return uDevices;
}

/* 
	This function remove a device (bDevice) from the list of current connected device 
	Returns usedDevices so the global variable is always up to date !
*/
usedDevices *pop(char *address){
	if(uDevices != NULL){
		usedDevices *temp = uDevices;
		usedDevices *prec = NULL;
		while(temp != NULL){
			if(strcmp(temp->device->address, address) == 0){
				if(prec == NULL && temp->next == NULL){ // Only one device in the list
					free(temp->device->name);
					free(temp->device);
					free(temp);
					return NULL;
				}
				if(prec == NULL && temp->next != NULL){ // Remove first device of the list
					prec = temp->next;
					free(temp->device->name);
					free(temp->device);
					free(temp);
					return prec;
				}
				if(prec != NULL && temp->next != NULL){ // Remove a device in the list (not the latest)
					prec->next = temp->next;
					free(temp->device->name);
					free(temp->device);
					free(temp);
					return uDevices;
				}
				if(prec != NULL && temp->next == NULL){ // Remove the last device of the list
					prec->next = NULL;
					free(temp->device->name);
					free(temp->device);
					free(temp);
					return uDevices;
				}
			}
			prec = temp;
			temp = temp->next;
		}
	}

	return uDevices;
}

/*
	Check if the address is one of the device of uDevices
	With this function you can check if a device is/was detected
	after the last scan.
*/
int isUsed(char *address){
	if(uDevices == NULL){ return 0; }
	usedDevices *temp = uDevices;
	do{
		if(strcmp(temp->device->address, address) == 0)
			return 1;
		temp = temp->next;
	}while(temp->next != NULL);
	return 0;
}

int uSize(usedDevices *devices){
	usedDevices *temp = devices;
	if(devices==NULL){return 0;}
	int i = 1;
	while(temp->next != NULL){
		i++;
		temp = temp->next;
	}
	return i;
}


/*
	Frees char ** array, the array must be null ended if "size" < 0.
	If "size" > 0, cleans the "size" first array
*/
void freedChar(char **array, int size){
	int i = 0;
	if(size == -1){
		while(array != NULL && array[i] != NULL){
			free(array[i]);
			i++;
		}
	}else{
		for(i=0; i < size; i++){
			free(array[i]);
		}
	}
	free(array);
}

/*
	Cleans double array of bDevices, must be null ended if "size" < 0.
	Cleans "size" devices if size > 0
*/
void freedDevices(bDevice **devices, int size){
	int i = 0;
	if(size == -1){
		while(devices[i] != NULL){
			free(devices[i]->name);
			free(devices[i]);
			i++;
		}
	}else{
		for(i = 0; i < size; i++){
			free(devices[i]->name);
			free(devices[i]);
		}
	}
	free(devices);
}

/*
	Same as the previous, but for device configuration
*/
void freedConfig(dConfig **configs, int size){
	int i = 0;
	if(size == -1){
		while(configs[i] != NULL){
			freedChar(configs[i]->startScripts, -1);
			freedChar(configs[i]->stopScripts, -1);
			free(configs[i]->device->name);
			free(configs[i]);
			i++;
		}
	}else{
		for(i=0; i < size; i++){
			freedChar(configs[i]->startScripts, -1);
			freedChar(configs[i]->stopScripts, -1);
			free(configs[i]);
		}
	}
	free(configs);
}


/*
	This function scans the nearby available bluetooth devices.
	Discovers a maximum of MAX_DEVICES devices.
*/
bDevice **scanDevices(){
	inquiry_info *targets;
	bDevice **devices;
	char buffer[MAX_NAME_LENGTH] = { 0 };
	int adaptor, connection, nbDevice, i;
	int timeout = 8;
	int flag = IREQ_CACHE_FLUSH; // Always flush the bluetooth scan before scanning

	adaptor = hci_get_route(NULL); // First available local bluetooth adaptor
	if(adaptor < 0){
		perror("Please connect a bluetooth adaptor");
		exit(-1);
	}
	connection = hci_open_dev(adaptor); // Bluetooth socket

	if(connection < 0){
		perror("Unable to open socket");
		exit(-1);
	}
	else{
		targets = (inquiry_info*)malloc(MAX_DEVICES * sizeof(inquiry_info));
		nbDevice = hci_inquiry(adaptor, timeout, MAX_DEVICES, NULL, &targets, flag); // Start scanning for nearby devices
		if(nbDevice < 0){
			perror("No device available");
		}else{
			devices = malloc((nbDevice+1) * sizeof devices[0]);
			for(i = 0; i < nbDevice; i++){
				devices[i] = malloc(sizeof(bDevice));
				memset(devices[i]->address, 0, sizeof(devices[i]->address));
				ba2str(&(targets+i)->bdaddr, devices[i]->address); // Stores the addres in a hex format
				memset(buffer, 0, sizeof(buffer));
				if(hci_read_remote_name(connection, &(targets+i)->bdaddr, sizeof(buffer), buffer, 0) < 0) // Human readable device name
					strcpy(buffer, "[Unknow]");
				devices[i]->name = strdup(buffer);
			}
			devices[i] = NULL;
		}
		free(targets);

	}
	close(connection);
	return devices;
}

/*
	Creates empty configuration file in $HOME named .proximity
	Keeps this wrong Indentation so the structure of the file looks clearer
	This function uses libconfig
*/
int generateEmptyConfig(char *file){
  config_t cfg;
  config_setting_t *root, *devices, *device, *mac, *name;
  config_init(&cfg);

  root = config_root_setting(&cfg);
  	devices = config_setting_add(root, "devices", CONFIG_TYPE_LIST);
  		device = config_setting_add(devices, "", CONFIG_TYPE_GROUP);
  			mac = config_setting_add(device, "MAC", CONFIG_TYPE_STRING);
  				config_setting_set_string(mac, "FF:FF:FF:FF:FF:FF");
  			name = config_setting_add(device, "Name", CONFIG_TYPE_STRING);
  				config_setting_set_string(name, "Default Name");
  			config_setting_add(device, "Start", CONFIG_TYPE_ARRAY);
  			config_setting_add(device, "Stop", CONFIG_TYPE_ARRAY);

  if(! config_write_file(&cfg, file))
  {
    printf("Error while writing default configuration file.\n");
    config_destroy(&cfg);
    return 0;
  }

  printf("New configuration successfully written to: %s\n", file);

  config_destroy(&cfg);
  return 1;
}

/*
	Uses libconfig
	Returns a string array with all the scripts of the sections (Start or Stop)
	The array ends by NULL.
*/
char **loadScripts(config_setting_t *node){
	int size = config_setting_length(node), i = 0;
	char **scripts = malloc(sizeof scripts[0]);
	for(i = 0; i < size; i++){
		const char* script = config_setting_get_string_elem(node,i);
		scripts[i] = strdup(script);
		scripts = realloc(scripts, (i+2)*sizeof(*scripts));
	}
	scripts[i] = NULL;
	return scripts;
}

/* 
	Loads configuration from file : $HOME/.proximity
	Returns an array of dConfig filled for each device
	Tries to create the configuration file if it doesn't exist
	Uses libconfig
*/
dConfig **loadConfig(){ 
	config_t cfg;
	char *home = getenv("HOME");
	char file[] = "/.proximity";
	char buffer[MAX_NAME_LENGTH] = { 0 };
	int nbDevices = 0, i = 0;
	dConfig **configuration = NULL;
	config_setting_t *devices;


	home = memcpy(malloc(strlen(home)+strlen(file)+1), home, strlen(home)+1);
	strcat(home, file);
	config_init(&cfg);

	if(access(home, F_OK) == 0){
		if(!config_read_file(&cfg, home)){
			perror("Can't open the file");
		}else{
			devices = config_lookup(&cfg, "devices");
			if(devices != NULL){
				nbDevices = config_setting_length(devices);
				configuration = malloc((nbDevices+1)*sizeof(configuration[0]));
				for(i = 0; i < nbDevices; i++){
					const char *address, *name;
					config_setting_t *device = config_setting_get_elem(devices, i);
					configuration[i] = malloc(sizeof(dConfig));
					bDevice *deviceSetup = malloc(sizeof(bDevice));
					if(!config_setting_lookup_string(device, "MAC", &address)){
						perror("Must set a valid MAC address for device");
					}
					memcpy(deviceSetup->address, address, BT_ADDRESS_LENGTH);
					if(!config_setting_lookup_string(device, "Name", &name)){
						perror("Must set a valid Name for device");
					}
					deviceSetup->name = strdup(name);
					configuration[i]->device = deviceSetup;
					config_setting_t *scripts = config_setting_get_member(device, "Start");
					if(scripts != NULL && config_setting_length(scripts)){
						configuration[i]->startScripts = loadScripts(scripts);
					}else{
						configuration[i]->startScripts = NULL;
					}
					scripts = config_setting_get_member(device, "Stop");
					if(scripts != NULL && config_setting_length(scripts)){
						configuration[i]->stopScripts = loadScripts(scripts);
					}else{
						configuration[i]->stopScripts = NULL;
					}
				}
			}
			configuration[i] = NULL;
		}
	}else{
		generateEmptyConfig(home);
		exit(0);
	}
	free(home);
	config_destroy(&cfg);
	return configuration;
}

/*
	The two following functions launch the scripts stored in the configuration file
*/
void launchstartScripts(dConfig *config){
	int i = 0, status;
	char *args[] = {"bash", "-c", NULL, NULL};
	pid_t child;
	while(config->startScripts != NULL && config->startScripts[i] != NULL){
		child = fork();
		if(child != 0){
			wait(&status);
		}else{
			if(access(config->startScripts[i], F_OK|X_OK) == 0){
				printf("Launching : %s\n", config->startScripts[i]);
				args[2] = config->startScripts[i];
				execvp(args[0], args);
			}else{
				printf("Unable to launch : %s\n", config->startScripts[i]);
			}
		}
		i++;
	}
}

void launchstopScripts(dConfig *config){
	int i = 0, status;
	char *args[] = {"bash", "-c", NULL, NULL};
	pid_t child;
	while(config->stopScripts != NULL && config->stopScripts[i] != NULL){
		child = fork();
		if(child != 0){
			wait(&status);
		}else{
			if(access(config->stopScripts[i], F_OK|X_OK) == 0){
				printf("Launching : %s\n", config->stopScripts[i]);
				args[2] = config->stopScripts[i];
				execvp(args[0], args);
			}else{
				printf("Unable to launch : %s\n", config->stopScripts[i]);
			}
		}
		i++;
	}
}

/* 
	Checks if a detected device is not available anymore
	Is used to detect if we need to start the "stop scripts"
*/
int inDevices(bDevice **devices, char *address){
	int i = 0;
	while(devices[i] != NULL){
		if(strcmp(devices[i]->address, address) == 0)
			return 1;
		i++;
	}
	return 0;
}

/*
	Checks if there is an available configuration for the device in the config
*/
dConfig *findConfig(dConfig **config, char *address){
	int i = 0;
	while(config[i] != NULL){
		if(strcmp(config[i]->device->address, address) == 0){
			return config[i];
		}
		i++;
	}
	return NULL;

}

/* 
	Cleans the uDevices list, removes devices which aren't detected anymore.
	Returns usedDevices to keep the global uDevices up to date 
*/
usedDevices *cleanUDevices(bDevice **devices, dConfig **config){
	if(uDevices == NULL){return uDevices;}
	usedDevices *temp = uDevices;
	while(temp != NULL){
		if(!inDevices(devices, temp->device->address)){
			printf("Removing %s\n", temp->device->address );
			launchstopScripts(findConfig(config, temp->device->address ));
			uDevices = pop(temp->device->address);
			temp = uDevices;
		}
		if(temp != NULL)
			temp = temp->next;
	}
	return uDevices;
}

/*
	If a new device is discovered, launchs the "Start Scripts" and append it to
	the uDevices list
	Returns usedDevices to keep the global uDevices up to date 
*/
usedDevices *detectChanges(bDevice **devices, dConfig **config){
	int i = 0, j = 0;
	while(config[i] != NULL){
		j = 0;
		while(devices[j] != NULL){
			if(strcmp(config[i]->device->address, devices[j]->address) == 0 && !isUsed(devices[j]->address)){
				uDevices = append(devices[j]);
				launchstartScripts(config[i]);	
			}
			j++;
		}
		i++;
	}
	uDevices = cleanUDevices(devices, config);
	return uDevices;
}

/*
	Callback of the SIGEV_SIGNAL, each 20s
	Loads the configuration from file
	Starts to scan for nearby devices
	Checks if there are matches between the two (and with uDevices)
*/
void scan(int sig, siginfo_t *si, void *uc){
	dConfig **config = loadConfig();
	bDevice **devices = scanDevices();
	uDevices = detectChanges(devices, config);
	freedDevices(devices, -1);
	freedConfig(config, -1);
}


int main(int argc, char **argv){
	timer_t 		 	timer;
	struct sigaction 	sa;
	struct sigevent	 	se;
	struct itimerspec	valueTimer;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_flags 		= SA_SIGINFO;
	sa.sa_sigaction 	= scan;
	sigemptyset(&sa.sa_mask);
	if(sigaction(SIGNAL, &sa, NULL) == -1)
		perror("Sigaction");

	memset(&se, 0, sizeof(struct sigevent));
	se.sigev_notify 		 = SIGEV_SIGNAL;
	se.sigev_signo  		 = SIGNAL;
	se.sigev_value.sival_ptr = &timer;
	if(timer_create(CLOCK_REALTIME, &se, &timer) == -1)
		perror("timer_create");
	
	valueTimer.it_interval.tv_sec  = WAIT_TIME;
	valueTimer.it_interval.tv_nsec = 0;
	valueTimer.it_value.tv_sec 	   = valueTimer.it_interval.tv_sec;
	valueTimer.it_value.tv_nsec	   = valueTimer.it_interval.tv_nsec;
	if(timer_settime(timer, 0, &valueTimer, NULL) == -1)
		perror("timer_settime");

	for(;;)
		pause();	

	return 0;
}

