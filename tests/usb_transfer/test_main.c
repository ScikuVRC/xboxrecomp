/* Exercise the real private bus walker with synthetic descriptors, no game. */
#undef NDEBUG
#define xbox_GetMappedSize usb_test_mapped_size
#define xbox_GetMemoryOffset usb_test_memory_offset
#define usb_gamepad_report_peek usb_test_report_peek
#define usb_gamepad_report_delivered usb_test_report_delivered
#include "../../src/usb/ohci.c"
#undef xbox_GetMappedSize
#undef xbox_GetMemoryOffset
#undef usb_gamepad_report_peek
#undef usb_gamepad_report_delivered
#include <assert.h>
#include "input_host.h"
#ifdef XBOXRECOMP_HAVE_SDL3
#include <SDL3/SDL.h>
#endif
extern int usb_gamepad_report_peek(int pad,uint8_t *out,int max,uint64_t *token);
extern void usb_gamepad_report_delivered(int pad,uint64_t token);
static int fake_report, delivered_count, delivered_pad;
static uint64_t delivered_token;
int usb_test_report_peek(int pad,uint8_t *out,int max,uint64_t *token)
{
    if (!fake_report) return usb_gamepad_report_peek(pad,out,max,token);
    assert(max>=20);memset(out,0,20);out[1]=20;out[6]=255;*token=0xABCu;return 20;
}
void usb_test_report_delivered(int pad,uint64_t token)
{
    if (!fake_report) { usb_gamepad_report_delivered(pad,token);return; }
    delivered_count++;delivered_pad=pad;delivered_token=token;
}
extern ptrdiff_t g_xbox_mem_offset;
size_t usb_test_mapped_size(void) { return 16u*1024u*1024u; }
recomp_func_t recomp_lookup(uint32_t address) { (void)address; abort(); }
void *recomp_lookup_manual(uint32_t address) { (void)address; abort(); }
static uint8_t *memory;
ptrdiff_t usb_test_memory_offset(void) { return (ptrdiff_t)memory; }
static void td_at(uint32_t td, unsigned direction, uint32_t buffer, unsigned length, uint32_t next)
{
    wr32(td,direction<<19);wr32(td+4,length?buffer:0);wr32(td+8,next);wr32(td+12,length?buffer+length-1:0);
}
static void setup_at(unsigned fa, unsigned request, unsigned value, unsigned length)
{
    UsbSetup packet={0x80,(uint8_t)request,(uint16_t)value,0,(uint16_t)length};
    memcpy(memory+0x3000,&packet,8);td_at(0x2100,TD_DP_SETUP,0x3000,8,0);
    assert(ohci_do_td(&s_hc[0],fa,0x2100)==0);
}
static void address_pad(int pad,unsigned address)
{
    UsbSetup packet={0,5,(uint16_t)address,0,0};
    assert(usb_gamepad_control(pad,&packet,NULL,0)==0);
    packet.bRequest=9;packet.wValue=1;assert(usb_gamepad_control(pad,&packet,NULL,0)==0);
}
static int test_script_axes(int virt)
{
    /* Fixed script for the process: the sampler may read it concurrently.
     * Distinct indexed diagonals cover all signs, slots and both sticks. */
    static const int16_t expected[4][4] = {
        {32767,32767,32767,32767}, {-32767,-32767,-32767,-32767},
        {-32767,32767,32767,-32767}, {32767,-32767,-32767,32767}
    };
    InputHostConfig cfg;
    _putenv_s("RECOMP_PAD_HOST",virt?"1":"0");
    _putenv_s("RECOMP_PAD_MIN_HOLD_MS","0");
    _putenv_s("RECOMP_PAD_SCRIPT_FORMAT","seconds");
    _putenv_s("RECOMP_PAD_SCRIPT_VIRTUAL",virt?"1":"");
    _putenv_s("RECOMP_PAD_SCRIPT",
        "lright:0:9999,lup:0:9999,rright:0:9999,rup:0:9999,"
        "p2-lleft:0:9999,p2-ldown:0:9999,p2-rleft:0:9999,p2-rdown:0:9999,"
        "p3-lleft:0:9999,p3-lup:0:9999,p3-rright:0:9999,p3-rdown:0:9999,"
        "p4-lright:0:9999,p4-ldown:0:9999,p4-rleft:0:9999,p4-rup:0:9999");
    if (virt) {
#ifdef XBOXRECOMP_HAVE_SDL3
        assert(SDL_Init(SDL_INIT_GAMEPAD));
        xbox_HostInputDefaults(&cfg);
        cfg.no_pads=1;cfg.virtual_pads=4;cfg.keyboard=INPUT_HOST_KEYBOARD_OFF;
        cfg.rumble_on_connect=0;cfg.ignore_focus=1;
        cfg.padmap.deadzone_left=cfg.padmap.deadzone_right=0;
        assert(xbox_HostInputStart(&cfg)==4);
#else
        abort();
#endif
    }
    for (int pad=0;pad<4;pad++) {
        uint8_t report[20];
        DWORD deadline=GetTickCount()+1000;
        int matched=0;
        do {
            assert(usb_gamepad_report(pad,report,20)==20);
            matched=1;
            for (int axis=0;axis<4;axis++) {
                int16_t value;
                memcpy(&value,report+12+axis*2,2);
                if (virt) {
                    /* Radial mapping limits a full diagonal to the unit circle. */
                    if ((expected[pad][axis]>0 && value<20000) ||
                        (expected[pad][axis]<0 && value>-20000)) matched=0;
                } else if (value!=expected[pad][axis]) matched=0;
            }
            if (!matched) Sleep(1);
        } while (!matched && (int32_t)(deadline-GetTickCount())>0);
        assert(matched);
        for (int byte=2;byte<12;byte++) assert(report[byte]==0);
    }
    puts(virt?"PASS seconds-script stick axes through SDL/host/USB, all four slots":
              "PASS seconds-script stick axes in USB reports, all four slots");
    /* Process exit owns the USB sampler; do not tear down its host first. */
    return 0;
}

int main(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"--script-axes")) return test_script_axes(0);
    if (argc==2 && !strcmp(argv[1],"--script-axes-virtual")) return test_script_axes(1);
    memory=VirtualAlloc(NULL,16u*1024u*1024u,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
    g_xbox_mem_offset=(ptrdiff_t)memory;
    for(unsigned va=0x1000;va<0x10000;va+=4096) xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)va);
    _putenv_s("RECOMP_PAD_HOST","0");_putenv_s("RECOMP_PAD_MIN_HOLD_MS","0");
    _putenv_s("RECOMP_PAD_SCRIPT_FORMAT","seconds");_putenv_s("RECOMP_PAD_SCRIPT","a:0:9999,p2-b:0:9999,p3-x:0:9999,p4-y:0:9999");
    ohci_reset(&s_hc[0],XBOX_OHCI0_BASE,0);s_plugged_pads=4;s_npads=4;
    UsbSetup hub={0,5,5,0,0}; assert(usb_hub_control(&hub,NULL,0)==0);
    hub.bmRequestType=0x23;hub.bRequest=3;hub.wIndex=1;hub.wValue=8;assert(usb_hub_control(&hub,NULL,0)==0);
    hub.wValue=4;assert(usb_hub_control(&hub,NULL,0)==0);
    for(int pad=0;pad<4;pad++) address_pad(pad,10+pad);
    assert(usb_route(5)==0);for(int pad=0;pad<4;pad++) assert(usb_route(10+pad)==pad+1);
    uint8_t descriptor[18];
    for(int pad=0;pad<4;pad++) {
        setup_at(10+pad,6,0x100,18);
        for(unsigned chunk=0,offset=0;chunk<3;chunk++) {
            unsigned n=chunk==2?2:8;td_at(0x2100,TD_DP_IN,0x4000,n,0);
            assert(ohci_do_td(&s_hc[0],10+pad,0x2100)==0);memcpy(descriptor+offset,memory+0x4000,n);offset+=n;
            if(chunk==0) {td_at(0x2100,TD_DP_OUT,0x5000,6,0);assert(ohci_do_td(&s_hc[0],(10+pad)|(2u<<7),0x2100)==0);}
        }
        assert(descriptor[0]==18&&descriptor[1]==1&&descriptor[7]==32&&descriptor[8]==0x5E&&descriptor[10]==0x89);
    }
    /* An invalid destination cannot consume the next control chunk. */
    setup_at(10,6,0x200,32);td_at(0x2100,TD_DP_IN,0xFFFFFFF0u,8,0);
    assert(ohci_do_td(&s_hc[0],10,0x2100)==12);assert(g_ctrl_sent_[1]==0);
    td_at(0x2100,TD_DP_IN,0x4000,8,0);assert(ohci_do_td(&s_hc[0],10,0x2100)==0);assert(memory[0x4000]==9&&memory[0x4001]==2);
    /* Indexed scripts and resets never drain another pad's report. */
    for(int pad=0;pad<4;pad++) {uint8_t report[20];assert(usb_gamepad_report(pad,report,20)==20);assert(report[4+pad]==255);for(int other=0;other<4;other++)if(other!=pad)assert(report[4+other]==0);}
    s_report_sent[0]=1;hub.wValue=4;assert(usb_hub_control(&hub,NULL,0)==0);address_pad(0,10);
    for(int pad=1;pad<4;pad++) assert(usb_gamepad_address(pad)==10+pad);
    /* Control, bulk-like and periodic lists build one done queue. */
    uint32_t done=0;
    for(unsigned pad=0;pad<3;pad++) {
        uint32_t ed=0x6000+pad*0x100,td=ed+0x20,tail=ed+0x40;
        wr32(ed,(10+pad)|(1u<<7));wr32(ed+4,tail);wr32(ed+8,td);wr32(ed+12,0);
        td_at(td,TD_DP_IN,0x8000+pad*0x100,20,tail);
        assert(ohci_run_list(&s_hc[0],ed,pad==2,&done)==1);
    }
    assert(done==0x6220&&rd32(done+8)==0x6120&&rd32(0x6128)==0x6020&&rd32(0x6028)==0);
    s_hc[0].reg[HcHCCA/4]=0x1000;bus_lock();ohci_publish_done(&s_hc[0],done);bus_unlock();
    assert(rd32(0x1084)==done);ohci_write(&s_hc[0],HcInterruptStatus,INTR_WDH,4);
    assert(rd32(0x1084)==0&&!(s_hc[0].reg[HcInterruptStatus/4]&INTR_WDH));
    ohci_raise(&s_hc[0],INTR_WDH);assert(!(s_hc[0].reg[HcInterruptStatus/4]&INTR_WDH));
    /* Exercise the actual OHCI delivery branch with one retained report.
     * Invalid DMA may not commit; a retry may commit; an unchanged periodic
     * report NAK may not commit again. Queue retirement is a later guest ACK. */
    fake_report=1;s_report_sent[0]=0;done=0;
    wr32(0x6400,10|(1u<<7));wr32(0x6404,0x6440);wr32(0x6408,0x6420);wr32(0x640C,0);
    td_at(0x6420,TD_DP_IN,0xFFFFFFF0u,8,0x6440);
    assert(ohci_run_list(&s_hc[0],0x6400,1,&done)==1);
    assert((rd32(0x6420)>>28)==12 && !delivered_count && !s_report_sent[0]);
    wr32(0x6408,0x6420);td_at(0x6420,TD_DP_IN,0,0,0x6440);done=0;
    assert(ohci_run_list(&s_hc[0],0x6400,1,&done)==1 && !delivered_count && !s_report_sent[0]);
    wr32(0x6408,0x6420);td_at(0x6420,TD_DP_IN,0x9000,8,0x6440);done=0;
    assert(ohci_run_list(&s_hc[0],0x6400,1,&done)==1 && !delivered_count && !s_report_sent[0]);
    wr32(0x6408,0x6420);td_at(0x6420,TD_DP_IN,0x9000,20,0x6440);done=0;
    assert(ohci_run_list(&s_hc[0],0x6400,1,&done)==1);
    assert(delivered_count==1 && delivered_pad==0 && delivered_token==0xABCu && s_report_sent[0]);
    wr32(0x6408,0x6420);td_at(0x6420,TD_DP_IN,0x9000,20,0x6440);done=0;
    assert(ohci_run_list(&s_hc[0],0x6400,1,&done)==0 && delivered_count==1);
    puts("PASS hub/four-pad caches, split descriptors, rumble, DMA errors, indexed scripts/reset, shared done queue and retained-token commit/retry/NAK");
    /* Process exit owns the test sampler; do not free its shared memory first. */
    return 0;
}
