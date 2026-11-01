/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Exercise the compiled Wine bridges against the real title sink, without
 * starting Wine or requiring a console. Headers come from that Wine build. */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ntstatus.h"
#include "unix_private.h"
#include "../../wine/ps5/pw_wine_sink.h"
/* The two independent Unix libraries use the same enum terminator. */
#define unix_funcs_count xinput_unix_funcs_count
#include "xinput1_3/unixlib.h"
#undef unix_funcs_count

static const unixlib_entry_t *bus;
static struct bus_event event;

static void next_event(UINT type, UINT64 device)
{
    assert(bus[ps5_wait](&event)==STATUS_PENDING);
    assert(event.type==type && (!device || event.device==device));
}

static void start_device(UINT64 device)
{
    struct device_start_params params={.device=device};
    assert(bus[device_start](&params)==STATUS_SUCCESS);
}

static void remove_device(UINT64 device)
{
    struct device_remove_params params={.device=device};
    assert(bus[device_remove](&params)==STATUS_SUCCESS);
}

static void check_report(UINT64 device, unsigned button, int32_t lx)
{
    struct unix_device *iface=(void *)(uintptr_t)device;
    const struct hid_device_state *layout=&iface->hid_device_state;
    int16_t x;
    next_event(BUS_EVENT_TYPE_INPUT_REPORT,device);
    assert(event.input_report.length==layout->report_len);
    assert(event.input_report.buffer[layout->button_start+button/8]&(1u<<(button%8)));
    assert(layout->axis_sizes[0]==16);
    memcpy(&x,event.input_report.buffer+layout->axis_byte_offsets[0],sizeof(x));
    assert(x==lx);
}

static void haptics(UINT64 device, uint16_t left, uint16_t right)
{
    struct unix_device *iface=(void *)(uintptr_t)device;
    uint8_t bytes[9]={iface->hid_haptics.intensity_report};
    HID_XFER_PACKET packet={.reportBuffer=bytes,.reportBufferLen=sizeof(bytes),.reportId=bytes[0]};
    IO_STATUS_BLOCK io={0};
    memcpy(bytes+1,&left,2);
    memcpy(bytes+3,&right,2);
    iface->vtbl->set_output_report(iface,&packet,&io);
    assert(io.Status==STATUS_SUCCESS);
}

static void check_xinput(void *module)
{
    const unixlib_entry_t *calls=dlsym(module,"__wine_unix_call_funcs");
    const unixlib_entry_t *wow64=dlsym(module,"__wine_unix_call_wow64_funcs");
    struct xinput_pad_params params={0};
    struct xinput_rumble_params motors={.index=0,.left=100,.right=200};
    PwWinePad first,second;
    uint32_t left,right;
    assert(calls && wow64 && calls[unix_pad_init](NULL)==STATUS_SUCCESS);
    assert(pw_wine_pad(&first) && pw_wine_pad_indexed(1,&second));
    assert(calls[unix_pad_state](&params)==STATUS_SUCCESS);
    assert(params.state.buttons==first.buttons && params.state.thumb_lx==first.thumb_lx);
    assert(params.state.packet==first.packet);
    params.index=1;
    assert(wow64[unix_pad_state](&params)==STATUS_SUCCESS);
    assert(params.state.buttons==second.buttons && params.state.thumb_lx==second.thumb_lx);
    assert(params.state.packet==second.packet);
    params.index=2;
    assert(calls[unix_pad_state](&params)==STATUS_SUCCESS && !params.state.connected);
    assert(calls[unix_pad_rumble](&motors)==STATUS_SUCCESS);
    motors.index=1;motors.left=300;motors.right=400;
    assert(wow64[unix_pad_rumble](&motors)==STATUS_SUCCESS);
    assert(pw_wine_rumble(&left,&right) && left==100 && right==200);
    assert(pw_wine_rumble_indexed(1,&left,&right) && left==300 && right==400);
    motors.index=2;
    assert(calls[unix_pad_rumble](&motors)==STATUS_INVALID_PARAMETER);
    puts("XInput Unix and WoW64: two states and independent rumble passed");
}

int main(int argc, char **argv)
{
    void *xinput,*winebus;
    UINT64 first,second,reconnected;
    WCHAR serial[5];
    PwWinePad a={.buttons=0x1000,.thumb_lx=30000},b={.buttons=0x2000,.thumb_lx=-30000};
    uint32_t left,right;
    assert(argc==3);
    alarm(15);
    xinput=dlopen(argv[1],RTLD_NOW);
    if(!xinput){fprintf(stderr,"dlopen xinput: %s\n",dlerror());return 1;}
    winebus=dlopen(argv[2],RTLD_NOW);
    if(!winebus){fprintf(stderr,"dlopen winebus: %s\n",dlerror());return 1;}
    bus=dlsym(winebus,"__wine_unix_call_funcs");
    assert(bus && !pw_wine_set_pad_indexed(0,&a));
    assert(bus[ps5_init](NULL)==STATUS_SUCCESS);
    next_event(BUS_EVENT_TYPE_DEVICE_CREATED,0);
    first=event.device;
    memcpy(serial,event.device_created.desc.serialnumber,sizeof(serial));
    assert(event.device_created.desc.vid==0x045e && event.device_created.desc.pid==0x028e);
    start_device(first);
    check_report(first,0,30000);

    assert(!pw_wine_set_pad_indexed(1,&b));
    next_event(BUS_EVENT_TYPE_DEVICE_CREATED,0);
    second=event.device;
    assert(first!=second && event.device_created.desc.input==1);
    assert(memcmp(serial,event.device_created.desc.serialnumber,sizeof(serial)));
    start_device(second);
    check_report(second,1,-30000);
    check_xinput(xinput);
    haptics(second,500,600);
    assert(pw_wine_rumble_indexed(1,&left,&right) && left==500 && right==600);
    assert(!pw_wine_rumble(&left,&right) && left==100 && right==200);

    assert(!pw_wine_set_pad_indexed(1,NULL));
    next_event(BUS_EVENT_TYPE_DEVICE_REMOVED,second);
    assert(!pw_wine_set_pad_indexed(1,&b));
    next_event(BUS_EVENT_TYPE_DEVICE_CREATED,0);
    reconnected=event.device;
    assert(reconnected!=second && event.device_created.desc.input==1);
    start_device(reconnected);
    haptics(reconnected,700,800);
    remove_device(second);
    assert(pw_wine_rumble_indexed(1,&left,&right) && left==700 && right==800);
    check_report(reconnected,1,-30000);
    assert(!pw_wine_set_pad_indexed(0,NULL));
    next_event(BUS_EVENT_TYPE_INPUT_REPORT,first);
    assert(pw_wine_pad_indexed(1,&b) && b.buttons==0x2000);
    assert(bus[ps5_stop](NULL)==STATUS_SUCCESS);
    assert(bus[ps5_wait](&event)==STATUS_SUCCESS);
    remove_device(first);
    remove_device(reconnected);
    alarm(0);
    puts("HID: separate devices, reports, rumble and reconnect lifecycle passed");
    return 0;
}
