#include "common.h"
#include <libtwl/i2c/i2c.h>
#include <libtwl/i2c/i2cMcu.h>
#include "cam_ops.h"

static u8 sActiveCamera;
static u8 sCoarseIntegrationTime;

static void writeI2C(u8 device, u16 index, u16 data)
{
    rtos_lockMutex(&gI2cMutex);
    i2c_start(device, I2C_DIRECTION_WRITE);
    i2c_write(index >> 8, false);
    i2c_write(index & 0xFF, false);
    i2c_write(data >> 8, false);
    i2c_write(data & 0xFF, true);
    rtos_unlockMutex(&gI2cMutex);
}

static u16 readI2C(u8 device, u16 index)
{
    rtos_lockMutex(&gI2cMutex);
    i2c_start(device, I2C_DIRECTION_WRITE);
    i2c_write(index >> 8, false);
    i2c_write(index & 0xFF, true);
    i2c_start(device, I2C_DIRECTION_READ);
    u8 msb = i2c_read(false);
    u8 lsb = i2c_read(true);
    rtos_unlockMutex(&gI2cMutex);
    return (msb << 8) | lsb;
}

static void setApt(u16 index, u16 data)
{
    u16 current = readI2C(sActiveCamera, index);
    writeI2C(sActiveCamera, index, current | data);
}

static void clrApt(u16 index, u16 data)
{
    u16 current = readI2C(sActiveCamera, index);
    writeI2C(sActiveCamera, index, current & ~(data));
}

static void waitClrApt(u16 reg, u16 mask)
{
    while (readI2C(sActiveCamera, reg) & mask); //evaluates true whenever a bit is set
}

static void waitSetApt(u16 reg, u16 mask)
{
    while (!(readI2C(sActiveCamera, reg) & mask)); // evaluates true whenever a bit is clear
}

static void writeMCU(u16 index, u16 data)
{
    writeI2C(sActiveCamera, MCU_ADDRESS, index);
    writeI2C(sActiveCamera, MCU_DATA, data);
}

static u16 readMCU(u16 index)
{
    writeI2C(sActiveCamera, MCU_ADDRESS, index);
    u16 read = readI2C(sActiveCamera, MCU_DATA);
    return (read);
}

static void setMCU(u16 index, u16 data)
{
    u16 current = readMCU(index);
    writeMCU(index, current | data);
}

static void waitClrMCU(u16 reg, u16 mask)
{
    while (readMCU(reg) & mask); //evaluates true whenever a bit is set
}

static void initCamera(u8 device)
{
    sActiveCamera = device;
    writeI2C(sActiveCamera, 0x001A, 0x0003);
    writeI2C(sActiveCamera, 0x001A, 0x0000);
    writeI2C(sActiveCamera, 0x0018, 0x4028);
    writeI2C(sActiveCamera, 0x001E, 0x0201);
    writeI2C(sActiveCamera, 0x0016, 0x42DF);
    waitClrApt(0x0018, 0x4000);
    waitSetApt(0x301A, 0x0004);
    writeMCU(0x02F0, 0x0000);
    writeMCU(0x02F2, 0x0210);
    writeMCU(0x02F4, 0x001A);
    writeMCU(0x2145, 0x02F4);
    writeMCU(0xA134, 0x0001);
    setMCU(0xA115, 0x0002);
    writeMCU(0x2755, 0x0002);
    writeMCU(0x2757, 0x0002);
    writeI2C(sActiveCamera, 0x0014, 0x2145);
    writeI2C(sActiveCamera, 0x0010, 0x0111);
    writeI2C(sActiveCamera, 0x0012, 0x0000);
    writeI2C(sActiveCamera, 0x0014, 0x244B);
    writeI2C(sActiveCamera, 0x0014, 0x304B);
    waitSetApt(0x0014, 0x8000);
    clrApt(0x0014, 0x0001);
    writeMCU(0x2703, 0x0100);
    writeMCU(0x2705, 0x00C0);
    writeMCU(0x2707, 0x0280);
    writeMCU(0x2709, 0x01E0);
    writeMCU(0x2715, 0x0001);
    writeMCU(0x2719, 0x001A);
    writeMCU(0x271B, 0x006B);
    writeMCU(0x271D, 0x006B);
    writeMCU(0x271F, 0x02C0);
    writeMCU(0x2721, 0x034B);
    writeMCU(0xA20B, 0x0000);
    writeMCU(0xA20C, 0x0006);
    writeMCU(0x272B, 0x0001);
    writeMCU(0x272F, 0x001A);
    writeMCU(0x2731, 0x006B);
    writeMCU(0x2733, 0x006B);
    writeMCU(0x2735, 0x02C0);
    writeMCU(0x2737, 0x034B);
    setApt(0x3210, 0x0008);
    writeMCU(0xA208, 0x0000);
    writeMCU(0xA24C, 0x0020);
    writeMCU(0xA24F, 0x0070);
    if (sActiveCamera == I2C_DEVICE_CAMERA_FRONT)
    {
        writeMCU(0x2717, 0x0024);
        writeMCU(0x272D, 0x0024);
    }
    else
    {
        writeMCU(0x2717, 0x0025);
        writeMCU(0x272D, 0x0025);
    }
    if (sActiveCamera == I2C_DEVICE_CAMERA_FRONT)
    {
        writeMCU(0xA202, 0x0022);
        writeMCU(0xA203, 0x00BB);
    }
    else
    {
        writeMCU(0xA202, 0x0000);
        writeMCU(0xA203, 0x00FF);
    }
    setApt(0x0016, 0x0020);
    writeMCU(0xA115, 0x0072);
    writeMCU(0xA11F, 0x0001);
    if (sActiveCamera == I2C_DEVICE_CAMERA_FRONT)
    {
        writeI2C(sActiveCamera, 0x326C, 0x0900);
        writeMCU(0xAB22, 0x0001);
    }
    else
    {
        writeI2C(sActiveCamera, 0x326C, 0x1000);
        writeMCU(0xAB22, 0x0002);
    }
    writeMCU(0xA103, 0x0006);
    waitClrMCU(0xA103, 0x000F);
    writeMCU(0xA103, 0x0005);
    waitClrMCU(0xA103, 0x000F);

    //for reasons that are way beyond me, if I don't do this then the image gets messed up. RIP all of my color translation code. (mostly same code as aptinaDeactivate)
    clrApt(0x001A, 0x0200);
    setApt(0x0018, 0x0001);
    waitSetApt(0x0018, 0x4000);
    waitClrApt(0x301A, 0x0004);
}

void aptinaInit(u8 camera) //might want to init both at the same time instead as NO$ suggests, TODO
{
    initCamera(I2C_DEVICE_CAMERA_BACK);
    initCamera(I2C_DEVICE_CAMERA_FRONT);
    sActiveCamera = camera; //we set the current camera to the one specified
}

void aptinaActivate()
{
    clrApt(0x0018, 0x0001);
    waitClrApt(0x0018, 0x4000);
    waitSetApt(0x301A, 0x0004);
    writeI2C(sActiveCamera, 0x3012, sCoarseIntegrationTime);
    setApt(0x001A, 0x0200);

    if (sActiveCamera == I2C_DEVICE_CAMERA_BACK)
    {
        mcu_writeReg(MCU_REG_CAMERA, 0);
    }
}

void aptinaDeactivate()
{
    clrApt(0x001A, 0x0200);
    setApt(0x0018, 0x0001);
    waitSetApt(0x0018, 0x4000);
    waitClrApt(0x301A, 0x0004);

    if (sActiveCamera == I2C_DEVICE_CAMERA_BACK)
    {
        mcu_writeReg(MCU_REG_CAMERA, 1);
    }
}

void aptinaSwitch()
{
    aptinaDeactivate();
    sActiveCamera = sActiveCamera == I2C_DEVICE_CAMERA_BACK
        ? I2C_DEVICE_CAMERA_FRONT
        : I2C_DEVICE_CAMERA_BACK;
    aptinaActivate();
}

void initParams(u8 coarse_int_time)
{
    sCoarseIntegrationTime = coarse_int_time;
}