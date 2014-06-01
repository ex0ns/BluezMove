/*  
  Compilation : gcc main.c -lrt -lbluetooth -lconfig -W -Wall
  Author      : ex0ns (http://ex0ns.me)
  Started     : April 2013
*/
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h> 
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <libconfig.h>


#define MAX_DEVICES         10
#define MAX_NAME_LENGTH     248
#define BT_ADDRESS_LENGTH   18
#define WAIT_TIME           2
#define CONFIG_FILE         "/.bluezmove"
#define LOCK_FILE           "/tmp/bluezmove.lock"
#define LOG_NAME            "BluezMove"

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
  usedDevices *newDevice;
  bDevice *copy;

  if((newDevice = malloc(sizeof(usedDevices))) == NULL)
    exit(1);
  if((copy = malloc(sizeof(bDevice))) == NULL)
    exit(1);
  
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
    syslog(LOG_ERR, "%s", strerror(errno));
    exit(-1);
  }
  connection = hci_open_dev(adaptor); // Bluetooth socket

  if(connection < 0){
    syslog(LOG_ERR, "%s", strerror(errno));
    exit(-1);
  }
  else{
    if((targets = (inquiry_info*)malloc(MAX_DEVICES * sizeof(inquiry_info))) == NULL)
      exit(0);
    nbDevice = hci_inquiry(adaptor, timeout, MAX_DEVICES, NULL, &targets, flag); // Start scanning for nearby devices
    if(nbDevice < 0){
      syslog(LOG_ERR, "%s", strerror(errno));
    }else{
      if((devices = malloc((nbDevice+1) * sizeof devices[0])) == NULL)
        exit(1);
      for(i = 0; i < nbDevice; i++){
        if((devices[i] = malloc(sizeof(bDevice))) == NULL)
          exit(1);
        memset(devices[i]->address, 0, sizeof(devices[i]->address));
        ba2str(&(targets+i)->bdaddr, devices[i]->address); // Stores the addres in a hex format
        memset(buffer, 0, sizeof(buffer));
        if(hci_read_remote_name(connection, &(targets+i)->bdaddr, sizeof(buffer), buffer, 0) < 0) // Human readable device name
          strcpy(buffer, "[Unknow]");
        devices[i]->name = strdup(buffer);
        syslog(LOG_INFO, "Device detected: %s", devices[i]->name);
      }
      devices[i] = NULL;
    }
    free(targets);

  }
  close(connection);
  return devices;
}

/*
  Creates empty configuration file in $HOME named .bluezmove
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
    syslog(LOG_ERR, "Error while writing default configuration file, %s\n", file);
    config_destroy(&cfg);
    return 0;
  }

  syslog(LOG_ERR, "New configuration successfully written to: %s\n", file);

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
  char **scripts;
  if((scripts = malloc(sizeof scripts[0])) == NULL)
    exit(0);
  for(i = 0; i < size; i++){
    const char* script = config_setting_get_string_elem(node,i);
    scripts[i] = strdup(script);
    if((scripts = realloc(scripts, (i+2)*sizeof(*scripts))) == NULL)
      exit(1);
  }
  scripts[i] = NULL;
  return scripts;
}

/* 
  Loads configuration from file : $HOME/.bluezmove
  Returns an array of dConfig filled for each device
  Tries to create the configuration file if it doesn't exist
  Uses libconfig
*/
dConfig **loadConfig(){ 
  config_t cfg;
  char *home = getenv("HOME");
  int nbDevices = 0, i = 0;
  dConfig **configuration = NULL;
  config_setting_t *devices;


  home = memcpy(malloc(strlen(home)+strlen(CONFIG_FILE)+1), home, strlen(home)+1);
  strcat(home, CONFIG_FILE);
  config_init(&cfg);

  if(access(home, F_OK) == 0){
    if(!config_read_file(&cfg, home)){
      syslog(LOG_ERR, "%s", strerror(errno));
    }else{
      devices = config_lookup(&cfg, "devices");
      if(devices != NULL){
        nbDevices = config_setting_length(devices);
        if((configuration = malloc((nbDevices+1)*sizeof(configuration[0]))) == NULL)
          exit(1);
        for(i = 0; i < nbDevices; i++){
          const char *address, *name;
          bDevice *deviceSetup;

          config_setting_t *device = config_setting_get_elem(devices, i);
          
          if((configuration[i] = malloc(sizeof(dConfig))) == NULL)
            exit(1);
          if((deviceSetup = malloc(sizeof(bDevice))) == NULL)
            exit(1);
          if(!config_setting_lookup_string(device, "MAC", &address)){
            syslog(LOG_ERR, "%s", strerror(errno));
          }
          memcpy(deviceSetup->address, address, BT_ADDRESS_LENGTH);
          if(!config_setting_lookup_string(device, "Name", &name)){
            syslog(LOG_ERR  , "%s", strerror(errno));
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
        syslog(LOG_INFO, "Starting command: %s", config->startScripts[i]);
        args[2] = config->startScripts[i];
        execvp(args[0], args);
      }else{
        syslog(LOG_INFO, "Unable to start command: %s", config->startScripts[i]);
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
        syslog(LOG_INFO, "Starting command: %s", config->stopScripts[i]);
        args[2] = config->stopScripts[i];
        execvp(args[0], args);
      }else{
        syslog(LOG_INFO, "Unable to start command: %s", config->stopScripts[i]);
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
      syslog(LOG_INFO, "Removing device: %s", temp->device->address);
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
  Called every WAIT_TIME seconds
  Loads the configuration from file
  Starts to scan for nearby devices
  Checks if there are matches between the two (and with uDevices)
*/
void scan(dConfig **config){
  bDevice **devices = scanDevices();
  uDevices = detectChanges(devices, config);
  freedDevices(devices, -1);
}


void unlock(){
  unlink(LOCK_FILE);
}

void check_already_running(){
  struct flock fl;
  int fd;

  fd = open(LOCK_FILE, O_RDWR | O_CREAT, 0600);
  if (fd < 0)
    exit(1);
 
  // No needs to fill the PID field, not used by the lock anyway
  fl.l_start = 0;
  fl.l_len = 0;
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET; 
  if (fcntl(fd, F_SETLK, &fl) < 0) {
    printf("Another instance is already running\n");
    exit(0);
  }
  atexit(unlock);
}


int main(void){
  pid_t sid = 0;
  pid_t child, pid = 0;
  
  openlog(LOG_NAME, LOG_PID, LOG_USER);

  if((child = fork()) == 0){
    // Child process 
    sid = setsid();
    if(sid < 0){
      perror("Couln't create the session");
      exit(1);
    }
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    if((pid = fork()) != 0){
      exit(0);
    }
    //  GrandChild process
    dConfig **config = loadConfig();
    check_already_running();
    for(;;){
      sleep(WAIT_TIME);
      scan(config);
    } 
    freedConfig(config, -1);
  }else{ // Parent process
    exit(0);
  }

  return 0;
}

