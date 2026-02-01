#ifndef HELPER_FUNC_H
#define HELPER_FUNC_H

#include <stddef.h>   // for size_t

void data_parsing(const char *data, size_t data_len);

uint32_t uid_to_decimal(const char *uid);


//void print_number(int num);   // if want to writ eone more function


void rfid_add(const char *id);
void rfid_remove(const char *id);
void rfid_display_all(void);
bool rfid_exists(uint32_t id);



#endif
