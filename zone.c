#define _GNU_SOURCE

#include <libzbd/zbd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "zoned.h"

//struct zone_node *_nodes_head = NULL;

/*
 * Command line options and device information.
 */
struct zbd_opts {
        /* Common options */
        char                    *dev_path;
        char                    *dump_path;
        char                    *dump_prefix;
        struct zbd_info         dev_info;
        long long               ofst;
        long long               len;
        size_t                  unit;

        /* Report zones options */
        bool                    rep_csv;
        bool                    rep_num_zones;
        bool                    rep_capacity;
        bool                    rep_dump;
        enum zbd_report_option  rep_opt;
};


static void zbd_zone_quick_print(struct zbd_zone *z,struct file_descriptor *fd){

    unsigned int zno = zbd_zone_start(z) / fd->info.zone_size;
    printf("Zone %05u, ofst %014llu, len %014llu, cap %014llu, wp %014llu, state %s\n",
        zno,
        zbd_zone_start(z),
        zbd_zone_len(z),
        zbd_zone_capacity(z) ,
        zbd_zone_wp(z),
        zbd_zone_cond_str(z, true));
}

struct repo_zone_info zbd_zone_print_info(struct zbd_zone *z,struct file_descriptor *fd){
        struct repo_zone_info curr_zone;
        curr_zone.z_no=zbd_zone_start(z) / fd->info.zone_size;
        curr_zone.zbd_zone_info.start=zbd_zone_start(z);
        curr_zone.zbd_zone_info.len=zbd_zone_len(z);
        curr_zone.zbd_zone_info.capacity=zbd_zone_capacity(z);
        curr_zone.zbd_zone_info.wp=zbd_zone_wp(z);
        curr_zone.zbd_zone_cond_str=zbd_zone_cond_str(z, true);
        return curr_zone;
}


static void zbd_print_zone(struct zbd_opts *opts, struct zbd_zone *z)
{
        unsigned int zno = zbd_zone_start(z) / opts->dev_info.zone_size;

        printf("Start to print zone!\n");
        if (opts->rep_csv) {
                printf("%05u, %u, %014llu, %014llu, %014llu, %014llu, 0x%01x, %01d, %01d\n",
                       zno,
                       (unsigned int)zbd_zone_type(z),
                       zbd_zone_start(z) / opts->unit,
                       zbd_zone_len(z) / opts->unit,
                       zbd_zone_capacity(z) / opts->unit,
                       zbd_zone_wp(z) / opts->unit,
                       zbd_zone_cond(z),
                       zbd_zone_non_seq_resources(z) ? 1 : 0,
                       zbd_zone_rwp_recommended(z) ? 1 : 0);
                return;
        }

        if (zbd_zone_cnv(z)) {
                printf("Zone %05u: %s, ofst %014llu, len %014llu, cap %014llu\n",
                       zno,
                       zbd_zone_type_str(z, true),
                       zbd_zone_start(z) / opts->unit,
                       zbd_zone_len(z) / opts->unit,
                       zbd_zone_capacity(z) / opts->unit);
                return;
        }

        if (zbd_zone_seq(z)) {
                printf("Zone %05u: %s, ofst %014llu, len %014llu, cap %014llu, "
                       "wp %014llu, %s, non_seq %01d, reset %01d\n",
                       zno,
                       zbd_zone_type_str(z, true),
                       zbd_zone_start(z) / opts->unit,
                       zbd_zone_len(z) / opts->unit,
                       zbd_zone_capacity(z) / opts->unit,
                       zbd_zone_wp(z) / opts->unit,
                       zbd_zone_cond_str(z, true),
                       zbd_zone_non_seq_resources(z) ? 1 : 0,
                       zbd_zone_rwp_recommended(z) ? 1 : 0);
                return;
        }

        printf("Zone %05u: unknown type 0x%01x, ofst %014llu, len %014llu\n",
               zno, zbd_zone_type(z),
               zbd_zone_start(z) / opts->unit,
               zbd_zone_len(z) / opts->unit);
}

int zoned_device_open(const char *dev, struct file_descriptor *fd) {
  assert(NULL != fd);
  printf("zones: %d\n",fd->info.nr_zones);
  printf("device path is : %s\n",dev);
  fd->read = zbd_open(dev, O_RDONLY, &fd->info);
  printf("after zbd_open->zones: %d\n",fd->info.nr_zones);
  if (0 > fd->read) {
    perror("zbd_open read failed");
    return -ENOENT;
  }
  
  fd->write = zbd_open(dev, O_WRONLY | O_DIRECT, &fd->info);
  printf("zones: %d\n",fd->info.nr_zones);
  if (0 > fd->write) {
    perror("zbd_open write failed");
    return -ENOENT;
  }
  return 0;
}

static void zoned_nodes_free(struct zone_node *head) {
  struct zone_node *node = head;
  while (node) {
    struct zone_node *temp = node->next;

    free(node);
    node = temp;
  }
}

void zoned_device_close(struct file_descriptor *fd) {
  /*
  if (_nodes_head) {
    zoned_nodes_free(_nodes_head);
  }
  */
  //printf("%d %d\n", fd->read, fd->write);

  if (fd == NULL) {
    return;
  }

  if (0 < fd->read) {
    zbd_close(fd->read);
  }

  if (0 < fd->write) {
    zbd_close(fd->write);
  }
}

int zoned_device_reset_zone(int fd, struct zbd_zone *zone) {
  int ret;

  struct zbd_zone z = {0};
  unsigned int report = 1;

  assert(0 < fd);

  ret = zbd_reset_zones(fd, zone->start, zone->len);
  if (ret) {
    perror("zone reset failed");
    return ret;
  }

  zone->wp = zone->start;

  return 0;
}

int main(void) {
  struct zbd_zone *zone_arr = NULL;
  //struct printing_zone curr_zone;

  int ret = 0, i = 0;
  unsigned int reported_zones = 0;
  uint64_t addr_space_sz;
  struct file_descriptor *fd;
  struct zbd_opts opts;
  unsigned long long  sector=6375342080;
  unsigned long long  length = 671088640;
  unsigned int nz;
  struct repo_zone_info curr_zone_info;


  memset(&opts, 0, sizeof(struct zbd_opts));
  opts.unit = 1;

  fd = (struct file_descriptor *)malloc(sizeof(struct file_descriptor));
  printf("Before open /dev/nvme0n1 fd info: zones: %d\n",fd->info.nr_zones);
  assert(NULL != fd);

  ret = zoned_device_open("/dev/nvme0n1",fd);
  printf("Finish calling zoned_device_open,zones: %d\n",fd->info.nr_zones);
  printf("ret is: %d\n",ret);
  struct zbd_info *info = &fd->info;

  assert(NULL != info);
  

  //addr_space_sz = (uint64_t)info->nr_zones * info->zone_size;

  /*Allocate zone array for lba*/
  printf("zone capacity is %lld\n",fd->info.zone_size);
  nz = (length+fd->info.zone_size-1)/fd->info.zone_size;
  printf("nz= %d\n",nz);
  if(!nz)
    return 0;
  
  zone_arr = (struct zbd_zone *)calloc(nz,sizeof(struct zbd_zone));
  if (!zone_arr){
    printf("No memory\n");
    return 1;
  }

  ret=zbd_report_zones(fd->read,sector,length,ZBD_RO_ALL,zone_arr,&nz);
    if(ret!=0){
      printf("zbd_report_zones()failed %d\n",ret);
      ret = 1;
      goto out;
    }
  
  for (i=0;i<nz;i++){
      curr_zone_info = zbd_zone_print_info(&zone_arr[i],fd);
      printf("Zone %05u, ofst %014llu, len %014llu, cap %014llu, wp %014llu, state %s\n",
        curr_zone_info.z_no,
        curr_zone_info.zbd_zone_info.start,
        curr_zone_info.zbd_zone_info.len,
        curr_zone_info.zbd_zone_info.capacity,
        curr_zone_info.zbd_zone_info.wp,
        curr_zone_info.zbd_zone_cond_str);
  }
    

 //Get all zone Info
 
  printf("before list: %d,%ld,%d \n",fd->read,addr_space_sz,reported_zones);
  ret = zbd_list_zones(fd->read, 0, addr_space_sz, ZBD_RO_ALL, &zone_arr,
                       &reported_zones);
  printf("after list: %d,%ld,%d \n",fd->read,addr_space_sz,reported_zones);  
  opts.dev_info.zone_size = info->zone_size;

  printf("Calling zbd_zone_print_info\n"); 
  printf("zone size : %lld\n",fd->info.zone_size);
  printf("lba is: %lld\n",sector);
  i= sector / fd->info.zone_size;
        curr_zone_info = zbd_zone_print_info(&zone_arr[i],fd);
        
 
  for (int i = 0; i < reported_zones; i+=1) {
          curr_zone_info = zbd_zone_print_info(&zone_arr[i],fd);
          printf("Zone %05u, ofst %014llu, len %014llu, cap %014llu, wp %014llu, state %s\n",
            curr_zone_info.z_no,
            curr_zone_info.zbd_zone_info.start,
            curr_zone_info.zbd_zone_info.len,
            curr_zone_info.zbd_zone_info.capacity,
            curr_zone_info.zbd_zone_info.wp,
            curr_zone_info.zbd_zone_cond_str);
  }
  
  for (int i = 0; i < reported_zones; i+=1) {
          zbd_print_zone(&opts,&zone_arr[i]);
  }
  
  
  if (zone_arr) {
    free(zone_arr);
  }
  zoned_device_close(fd);
return 0;
exception:
  if (zone_arr) {
    free(zone_arr);
  }
  zoned_device_close(fd);
  return -errno;

out:
    free(zone_arr);
    return ret;
}
