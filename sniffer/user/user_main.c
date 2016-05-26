#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_config.h"
#include "user_interface.h"
#include "espconn.h"

struct RxControl {
    signed rssi:8; // signal intensity of packet
    unsigned rate:4;
    unsigned is_group:1;
    unsigned:1;
    unsigned sig_mode:2; // 0:is 11n packet; 1:is not 11n packet; 
    unsigned legacy_length:12; // if not 11n packet, shows length of packet. 
    unsigned damatch0:1;
    unsigned damatch1:1;
    unsigned bssidmatch0:1;
    unsigned bssidmatch1:1;
    unsigned MCS:7;
    // if is 11n packet, shows the modulation 
    // and code used (range from 0 to 76)
    unsigned CWB:1; // if is 11n packet, shows if is HT40 packet or not 
    unsigned HT_length:16;// if is 11n packet, shows length of packet.
    unsigned Smoothing:1;
    unsigned Not_Sounding:1;
    unsigned:1;
    unsigned Aggregation:1;
    unsigned STBC:2;
    unsigned FEC_CODING:1; // if is 11n packet, shows if is LDPC packet or not. 
    unsigned SGI:1;
    unsigned rxend_state:8;
    unsigned ampdu_cnt:8;
    unsigned channel:4; //which channel this packet in.
    unsigned:12;
};

struct LenSeq
{
	u16 len; // length of packet
	u16 seq; // serial number of packet, the high 12bits are serial number,
	// low 14 bits are Fragment number (usually be 0) 
	u8 addr3[6]; // the third address in packet 
};

struct sniffer_buf
{
	struct RxControl rx_ctrl;
	u8 buf[36 ]; // head of ieee80211 packet
	u16 cnt; // number count of packet 
	struct LenSeq lenseq[1]; //length of packet 
};

struct sniffer_buf2
{
	struct RxControl rx_ctrl;
	u8 buf[112];
	u16 cnt; 
	u16 len; //length of packet 
};


#define user_procTaskPrio        0
#define user_procTaskQueueLen    1
os_event_t    user_procTaskQueue[user_procTaskQueueLen];
#define printmac(buf, i) os_printf("\t%02X:%02X:%02X:%02X:%02X:%02X", buf[i+0], buf[i+1], buf[i+2], \
				    buf[i+3], buf[i+4], buf[i+5])

static volatile os_timer_t channelHop_timer;


static void loop(os_event_t *events);
static void promisc_cb(uint8 *buf, uint16 len);

#define FRAME_TYPE_MANAGEMENT 0
#define FRAME_TYPE_CONTROL    1
#define FRAME_TYPE_DATA       2

#define FRAME_SUBTYPE_MGMT_ASSOCIATION_REQUEST 0
#define FRAME_SUBTYPE_MGMT_PROBE_REQUEST       4
#define FRAME_SUBTYPE_MGMT_PROBE_RESPONSE      5
#define FRAME_SUBTYPE_MGMT_PROBE_BEACON        8

#define MAX_CHANNELS 13
int cs[MAX_CHANNELS];
int iters;
int counter;
int watermark;
                                          
void init_stats (void)
{
    int i;

    for (i = 0; i < MAX_CHANNELS; i++)
    {
        cs[i] = 0;
    }
}

struct cache_entry
{
    uint8 addr[6];
    int age;
};

static struct cache_entry cache[MAX_CACHE_ENTRIES];

void cache_flush (void)
{
    watermark = 0;
    bzero (cache, sizeof (cache));
}

int cache_new (uint8 *addr)
{
    int i;
    int oldest = 0;
    int oldest_age = 2^32-1;

    for (i = 0; i < MAX_CACHE_ENTRIES; i++)
    {
        // Compare cache entry
        if (memcmp (addr, cache[i].addr, 6) == 0)
        {
            // Found
            // os_printf("Cache hit for ");
            // printmac (addr, 0);
            // os_printf("\n");
            cache[i].age = counter;
            return 0;
        }

        // FIXME: Implement timeout
        // Find oldest entry
        if (cache[i].age < oldest_age)
        {
            oldest_age = cache[i].age;
            oldest = i;
        }
    }

    ++watermark;

    // Not found, add to cache
    memcpy (cache[oldest].addr, addr, 6);
    cache[oldest].age = counter;

    //os_printf("Cache miss (oldest=%d) for ", oldest);
    // printmac (addr, 0);
    // os_printf("\n");
    return 1;
}

int next_channel_index_from_index (int index)
{
    return (index + 1) % MAX_CHANNELS;

    //switch (index)
    //{
    //    case 0:  return  5;
    //    case 5:  return 10;
    //    case 10: return  0;
    //    default: return  0;
    //}
}

void hop_channel(void *arg) {
    int i, sum = 0, sum1611 = 0;
    int ChannelIndex = (wifi_get_channel() - 1);
    counter++;

    // Emit channel statistics only when back to channel 1
    if (ChannelIndex == 0 && iters > MAX_ITERATIONS)
    {
        for (i = 0; i < MAX_CHANNELS; i++)
        {
            sum += cs[i];
            os_printf(" %d, %4.4d", i + 1, cs[i]);
        }
        sum1611 = cs[0] + cs[5] + cs[10];
        os_printf("\n (main=%d, total=%d)\n", sum1611, sum);
        init_stats();
        cache_flush();
        iters = 0;
    } else
    {
        iters++;
        //os_printf(".");
    }

    wifi_set_channel(next_channel_index_from_index (ChannelIndex) + 1);
}

void hexdump (uint8 *buf, uint16 len)
{
    int i;

    os_printf ("000000 ");
    for (i = 0; i < len; i++)
    {
       os_printf("%02X ", buf[i]);
    }
    os_printf ("\n");
}

#define ADDR1(buf) (buf +  4)
#define ADDR2(buf) (buf + 10)
#define ADDR3(buf) (buf + 16)

enum { DIR_UNKNOWN, DIR_UP, DIR_DOWN };

int check_direction (int channel, uint8 *buf, uint16 len, uint8 *addr3)
{
    int dir = DIR_UNKNOWN;
    uint8 *sa, *da;
    int type;
    int subtype;
    int result;

    uint8 frame_control0 = buf[0];
    subtype = (frame_control0 >> 4) & 0xf;
    type    = (frame_control0 >> 2) & 0x3;

    uint8 frame_control1 = buf[1];
    int to_ds   = frame_control1 & 0x1;
    int from_ds = (frame_control1 >> 1) & 0x1;

    if (to_ds)
    {
        if (from_ds)
        { // to_ds=1, from_ds=1: Inter-DS frames (mesh, repeater, ...), ignore.
            return 0; // Ignore
        } else
        { // to_ds=1, from_ds=0: Frame from station to DS (upstream)
            sa  = ADDR2 (buf);
            da  = ADDR3 (buf);
            dir = DIR_UP;
        }
    } else // to_ds=0
    {
        da = ADDR1 (buf);

        if (from_ds)
        { // to_ds=0, from_ds=1: Frame forwarded by AP from DS to station
            if (addr3 != NULL)
            {
                if (memcmp (addr3, ADDR3 (buf), 6) != 0)
                {
                    os_printf ("addr3 differs (addr3/frame):");
                    printmac (addr3, 0);
                    printmac (ADDR3(buf), 0);
                    os_printf ("\n");
                }
            }
            sa  = (addr3 == NULL) ? ADDR3 (buf) : addr3;
            dir = DIR_DOWN;
        } else
        { // to_ds=0, from_ds=0: Inter station traffic or management/control frames
            sa = ADDR2 (buf);

            switch (type)
            {
                case FRAME_TYPE_MANAGEMENT:
                    switch (subtype)
                    {
                        case FRAME_SUBTYPE_MGMT_PROBE_REQUEST:  dir = DIR_UP; break;
                        case FRAME_SUBTYPE_MGMT_PROBE_RESPONSE: dir = DIR_DOWN; break;
                        case FRAME_SUBTYPE_MGMT_PROBE_BEACON:   return 0;
                        default: DIR_UNKNOWN;
                    }
                    break;
                default: dir = DIR_UNKNOWN;
            }
        }
    }

    if (dir == DIR_UP)
    {
        result = cache_new (sa);
    } else if (dir == DIR_DOWN)
    {
        result = cache_new (da);
    } else
    {
        result = cache_new (sa) || cache_new (da);
    }

    if (result)
    {
        os_printf (" %1.1x.%2.2x ch:%2.0d len:%3.0d fd:%1.1d td:%1.1d [%1.1d,%3.0d]", type, subtype, channel, len, from_ds, to_ds, result, watermark);
        printmac (sa, 0);
        os_printf (" => ");
        printmac (da, 0);
        os_printf ("\n");
    }

    if (from_ds == 0 && to_ds == 0 && type == 2)
    {
        hexdump (buf, len);
    }

    return result;
}

int dissect_buf (int channel, uint8 *buf, uint16 length)
{
    int result;

    if (length == 12)
    {
        // This is just an RxControl structure which has no useful information for us. Ignore it.
        //os_printf ("Ignoring 12 byte packet\n");
        return 0;
    } else if (length == 128)
    {
        // This packet contains a sniffer_buf2 structure
        struct sniffer_buf2 *sb2 = (struct sniffer_buf2 *)buf;
        if (sb2->cnt != 1)
        {
            os_printf ("sniffer_buf2 has cnt != 1 (%d)", sb2->cnt);
            return 0;
        }
        return check_direction (channel,sb2->buf, sb2->len, NULL);
    } else if (length > 0 && length % 10 == 0)
    {
        struct sniffer_buf *sb = (struct sniffer_buf *)buf;
        if (sb->cnt != 1)
        {
            os_printf ("Invalid sb of len %d\n", sb->cnt);
            return 0;
        }
        return check_direction (channel, sb->buf, sb->lenseq[0].len, sb->lenseq[0].addr3);
    } else
    {
        os_printf ("Invalid packet with %d bytes\n", length);
        return 0;
    }
}

static void ICACHE_FLASH_ATTR
promisc_cb(uint8 *buffer, uint16 length)
{
    int ChannelIndex;
    counter++;
    uint16 len;

    ChannelIndex = wifi_get_channel() - 1;
    if (!dissect_buf (ChannelIndex + 1, buffer, length))
    {
        return;
    }

    cs[ChannelIndex]++;
    //hexdump (buffer, length);
}

//Main code function
static void ICACHE_FLASH_ATTR
loop(os_event_t *events)
{
    os_delay_us(10);
}

void ICACHE_FLASH_ATTR
sniffer_init_done() {
    os_printf("Enter: sniffer_init_done");
    wifi_station_set_auto_connect(false); // do not connect automatically
    wifi_station_disconnect(); // no idea if this is permanent
    wifi_promiscuous_enable(false);
    wifi_set_promiscuous_rx_cb(promisc_cb);
    wifi_promiscuous_enable(true);
    init_stats();
    cache_flush();
    iters = 0;
    counter = 0;
    os_printf("done.\n");
    wifi_set_channel(1);
}

//Init function 
void ICACHE_FLASH_ATTR
user_init()
{
    uart_div_modify( 0, UART_CLK_FREQ / ( 115200 ) );
    wifi_set_opmode(0x1); // 0x1: station mode
    system_init_done_cb(sniffer_init_done);
    
    os_timer_disarm(&channelHop_timer);
    os_timer_setfn(&channelHop_timer, (os_timer_func_t *) hop_channel, NULL);
    os_timer_arm(&channelHop_timer, CHANNEL_HOP_INTERVAL, 1);
}
