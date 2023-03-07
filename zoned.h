#ifndef _ZONED_H
#define _ZONED_H

#include <libzbd/zbd.h>

struct file_descriptor {
  int read;
  int write;
  struct zbd_info info;
};

struct zone_node {
  struct zbd_zone zone;
  struct zone_node *next;
};

struct zone_list {
  struct zone_node *empty;
  struct zone_node *open;
  struct zone_node *close;
  struct zone_node *full;
};

struct repo_zone_info {
   unsigned int z_no;
   const char *zbd_zone_cond_str ;
   struct zbd_zone zbd_zone_info;
};

struct zone_usage{
  unsigned int empty_zones;
  unsigned int used_zones;
  unsigned int full_zones;
};

int zoned_device_open(const char *dev, struct file_descriptor *fd);
void zoned_device_close(struct file_descriptor *fd);
int zoned_device_reset_zone(int fd, struct zbd_zone *zone);
int zoned_device_init_zones(struct file_descriptor *fd);
extern struct repo_zone_info zbd_zone_print_info(struct zbd_zone *z,struct file_descriptor *fd);
#endif