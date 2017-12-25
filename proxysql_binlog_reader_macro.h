#define ioctl_FIONBIO(fd, mode) \
{ \
  int ioctl_mode=mode; \
  ioctl(fd, FIONBIO, (char *)&ioctl_mode); \
}
