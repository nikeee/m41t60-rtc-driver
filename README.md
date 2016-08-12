# M41T60 Driver
> Unmaintained university class project from 2016

The RTC chip is connected via I²C and the driver was written for kernel v2.6.36.

## Compile
It does not compile on newer versions due to [ioctl](https://stackoverflow.com/questions/60477482) being removed in the meantime.

However, you can try this:
```bash
cd src
make -C /lib/modules/$(uname -r)/build  M=$(pwd) modules
```
