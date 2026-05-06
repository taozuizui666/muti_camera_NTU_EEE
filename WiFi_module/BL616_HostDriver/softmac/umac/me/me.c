#include "rwnx_config.h"
#include "me.h"
#include "me_mgmtframe.h"
#include "vif_mgmt.h"

struct me_env_tag me_env;

struct mac_chan_def *me_freq_to_chan_def(uint8_t band, uint16_t freq)
{
    int i, chan_cnt;
    struct mac_chan_def *chan;

#if CFG_5G
    // Get the correct channel table
    chan = (band == PHY_BAND_2G4) ? me_env.chan.chan2G4 : me_env.chan.chan5G;
    chan_cnt = (band == PHY_BAND_2G4) ? me_env.chan.chan2G4_cnt : me_env.chan.chan5G_cnt;
#else
    chan = me_env.chan.chan2G4;
    chan_cnt = me_env.chan.chan2G4_cnt;
#endif

    for (i = 0; i < chan_cnt; i++)
    {
        if (chan[i].freq == freq)
            return &chan[i];
    }

    return NULL;
}

struct mac_chan_def *me_chan_id_to_chan_def(uint8_t band, uint8_t chan_id)
{
    uint16_t freq = phy_channel_to_freq(band, chan_id);
    if (!freq)
        return NULL;

    return me_freq_to_chan_def(band, freq);
}


