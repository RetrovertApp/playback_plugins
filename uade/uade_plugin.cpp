///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UADE Playback Plugin
//
// Implements RVPlaybackPlugin interface for Amiga music formats using UADE (Unix Amiga Delitracker Emulator).
// Based on UADE by Heikki Orsila and Michael Doering - https://gitlab.com/mvtiaine/uade
//
// Threading approach adapted from HippoPlayer by Daniel Collin.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>

#include <uade/eagleplayer.h>
#include <uade/uade.h>

// Forward-declare scope functions from audio.c (avoids pulling in internal UADE headers)
extern "C" {
void audio_scope_enable(int enable);
unsigned int audio_scope_get_data(int channel, float* buffer, unsigned int num_samples);
}

#include <stdlib.h>
#include <string.h>
#include <thread>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define OUTPUT_SAMPLE_RATE 48000

const RVLog* g_rv_log = nullptr;

// Base directory for UADE data files (players, eagleplayer.conf)
static char g_uade_base_dir[4096] = { 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Thread wrapper for UADE's threading model

struct ThreadWrapper {
    std::thread* thread = nullptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void uade_run_thread(void (*f)(void*), void* data, void* user_data) {
    ThreadWrapper* wrapper = (ThreadWrapper*)user_data;
    wrapper->thread = new std::thread(f, data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void uade_wait_thread(void* user_data) {
    ThreadWrapper* wrapper = (ThreadWrapper*)user_data;
    if (wrapper->thread) {
        wrapper->thread->join();
        delete wrapper->thread;
        wrapper->thread = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct UadeReplayerData {
    struct uade_state* state;
    ThreadWrapper thread_wrapper;
    int current_subsong;
    int num_subsongs;
    char current_path[4096];
    bool scope_enabled;
} UadeReplayerData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper to create UADE state with common configuration

static struct uade_state* create_uade_state(int spawn, ThreadWrapper* thread_wrapper) {
    struct uade_config* config = uade_new_config();
    if (!config) {
        rv_error("UADE: Failed to create config");
        return nullptr;
    }

    uade_config_set_option(config, UC_ONE_SUBSONG, nullptr);
    uade_config_set_option(config, UC_IGNORE_PLAYER_CHECK, nullptr);
    uade_config_set_option(config, UC_NO_EP_END, nullptr);
    uade_config_set_option(config, UC_FREQUENCY, "48000");
    uade_config_set_option(config, UC_BASE_DIR, g_uade_base_dir);

    struct uade_state* state = uade_new_state(config, spawn, thread_wrapper);
    free(config);

    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* uade_supported_extensions(void) {
    // UADE supports 350+ Amiga music formats (excluding formats handled by other plugins)
    return "40a,40b,41a,4v,50a,60a,61a,aam,abk,ac1,ac1d,adpcm,adsc,agi,alp,amc,aon,aon4,aon8,"
           "aps,arp,ash,ast,aval,avp,bd,bds,bfc,bp,bp3,bsi,bss,bye,chan,cin,cm,core,cp,cplx,"
           "crb,cus,cust,custom,dat,db,dh,di,dl,dl_deli,dlm1,dlm2,dln,dm,dm1,dm2,dmu,dmu2,"
           "dns,doda,dp,dsc,dsr,dss,dum,dw,dwold,dz,dzp,ea,emod,ems,emsv6,eu,ex,fc,fc-bsi,"
           "fc13,fc14,fc3,fc4,fcm,fp,fred,ft,ftm,fuz,fuzz,fw,glue,gm,gmc,gray,gv,hd,hip,hip7,"
           "hipc,hmc,hn,hot,hrt,ims,is,is20,ism,it1,jam,jb,jc,jcb,jcbo,jd,jmf,jo,jp,jpn,jpnd,"
           "jpo,jpold,js,jt,kef,kef7,kh,kim,kris,krs,ksm,lax,lion,lme,ma,max,mc,mcmd,mco,mcr,"
           "md,mdat,mdst,mexxmp,mfp,mg,midi,mii,mk2,mkii,mkiio,ml,mm4,mm8,mmd0,mmd1,mmd2,mmd3,"
           "mmdc,mod15,mod15_mst,mod15_st-iv,mod15_ust,mod3,mod_adsc4,mod_comp,mod_doc,mod_flt4,"
           "mod_ntk,mod_ntk1,mod_ntk2,mod_ntkamp,mok,mon,mon_old,mosh,mpro,mso,mth,mtp2,mug,mug2,"
           "mw,noisepacker2,noisepacker3,np,np1,np2,np3,npp,nr,nru,ntp,ntpk,octamed,okta,oldw,"
           "one,osp,oss,p10,p21,p30,p40a,p40b,p41a,p4x,p50a,p5a,p5x,p60,p60a,p61,p61a,p6x,pap,"
           "pat,pha,pin,pm,pm0,pm01,pm1,pm10c,pm18a,pm2,pm20,pm4,pm40,pmz,pn,polk,powt,pp10,"
           "pp20,pp21,pp30,ppk,pr1,pr2,prom,prt,pru,pru1,pru2,prun,prun1,prun2,ps,psa,psf,pt,"
           "puma,pvp,pwr,pyg,pygm,pygmy,qc,qpa,qts,rh,rho,riff,rj,rjp,rk,rkb,s7g,sa,sa-p,sa_old,"
           "sas,sb,sc,scn,scr,sct,scumm,sdata,sdc,sdr,sfx13,sfx20,sg,sid1,sid2,sj,sjs,skt,skyt,"
           "sm,sm1,sm2,sm3,smn,smod,smpro,smus,sndmon,sng,snk,soc,sog,sonic,spl,ss,st,st2,st30,"
           "star,stpk,sun,syn,synmod,tcb,tf,tfhd1.5,tfhd7v,tfhdpro,tfmx1.5,tfmx7v,tfmxpro,thm,"
           "thn,thx,tiny,tits,tme,tmk,tp,tp1,tp2,tp3,tpu,trc,tro,tron,tronic,tw,two,uds,ufo,"
           "un2,unic,unic2,vss,wb,wn,xan,xann,ym,ymst,zen,zm";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* uade_plugin_create(const RVService* service_api) {
    (void)service_api;

    UadeReplayerData* data = (UadeReplayerData*)calloc(1, sizeof(UadeReplayerData));
    if (!data) {
        return nullptr;
    }

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int uade_plugin_destroy(void* user_data) {
    UadeReplayerData* data = (UadeReplayerData*)user_data;
    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int uade_plugin_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)service_api;
    UadeReplayerData* data = (UadeReplayerData*)user_data;

    // Close any previous state
    if (data->state) {
        uade_stop(data->state);
        uade_cleanup_state(data->state, 1, &data->thread_wrapper);
        data->state = nullptr;
    }

    // Create new state with spawn=1 (threading enabled)
    data->state = create_uade_state(1, &data->thread_wrapper);
    if (!data->state) {
        rv_error("UADE: Failed to create state for %s", url);
        return -1;
    }

    // Store path for later use
    strncpy(data->current_path, url, sizeof(data->current_path) - 1);
    data->current_path[sizeof(data->current_path) - 1] = '\0';

    // Start playback
    int play_subsong = (subsong == 0) ? -1 : (int)subsong;
    if (uade_play(url, play_subsong, data->state) != 1) {
        rv_error("UADE: Failed to play %s", url);
        uade_cleanup_state(data->state, 1, &data->thread_wrapper);
        data->state = nullptr;
        return -1;
    }

    // Get song info for subsong count
    const struct uade_song_info* song_info = uade_get_song_info(data->state);
    if (song_info) {
        data->num_subsongs = song_info->subsongs.max - song_info->subsongs.min + 1;
        data->current_subsong = (play_subsong == -1) ? song_info->subsongs.def : play_subsong;
    } else {
        data->num_subsongs = 1;
        data->current_subsong = 1;
    }

    rv_info("UADE: Playing %s (subsong %d/%d)", url, data->current_subsong, data->num_subsongs);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void uade_plugin_close(void* user_data) {
    UadeReplayerData* data = (UadeReplayerData*)user_data;

    if (data->state) {
        uade_stop(data->state);
        uade_cleanup_state(data->state, 1, &data->thread_wrapper);
        data->state = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Check if a player name matches a format that has a dedicated plugin.
// Returns true if UADE should defer to the dedicated plugin (return Unsure instead of Supported).
static bool has_dedicated_plugin(const char* player_name) {
    if (!player_name) {
        return false;
    }

    // TFMX plugin handles: TFMX (all variants), Future Composer, Jochen Hippel formats
    if (strncmp(player_name, "TFMX", 4) == 0) {
        return true;
    }
    if (strncmp(player_name, "Future", 6) == 0) {
        return true;
    }
    if (strncmp(player_name, "Jochen", 6) == 0) {
        return true;
    }

    // Hively plugin handles: AHX (AbyssHighestExperience)
    if (strncmp(player_name, "Abyss", 5) == 0) {
        return true;
    }

    // OpenMPT handles: ProTracker, NoiseTracker, SoundTracker formats (MOD files)
    // UADE can play these but OpenMPT provides better compatibility
    if (strncmp(player_name, "Protracker", 10) == 0) {
        return true;
    }
    if (strncmp(player_name, "NoiseTracker", 12) == 0) {
        return true;
    }
    if (strncmp(player_name, "SoundTracker", 12) == 0) {
        return true;
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult uade_plugin_probe_can_play(uint8_t* file_data, uint64_t data_size, const char* filename,
                                                uint64_t total_size) {
    (void)total_size;

    // Reject C64 SID files (PSID/RSID) - these belong to sidplayfp plugin
    // UADE's eagleplayer incorrectly identifies them as Amiga SIDMon format
    if (data_size >= 4
        && ((file_data[0] == 'P' || file_data[0] == 'R') && file_data[1] == 'S' && file_data[2] == 'I'
            && file_data[3] == 'D')) {
        return RVProbeResult_Unsupported;
    }

    // Create a temporary state without spawning threads for probing
    ThreadWrapper temp_wrapper;
    struct uade_state* state = create_uade_state(0, &temp_wrapper);
    if (!state) {
        return RVProbeResult_Unsupported;
    }

    struct uade_detection_info detect_info;
    RVProbeResult result = RVProbeResult_Unsupported;

    if (uade_analyze_eagleplayer(&detect_info, file_data, (size_t)data_size, filename, strlen(filename), state) >= 0) {
        const char* player_name
            = (detect_info.ep && detect_info.ep->playername) ? detect_info.ep->playername : "unknown";

        // For formats with dedicated plugins, return Unsure so they get priority.
        // UADE will be used as fallback if the dedicated plugin fails or is unavailable.
        if (has_dedicated_plugin(player_name)) {
            result = RVProbeResult_Unsure;
            rv_debug("UADE: Format has dedicated plugin, deferring: %s (player: %s)", filename, player_name);
        } else {
            result = RVProbeResult_Supported;
            rv_debug("UADE: Supported format detected: %s (player: %s)", filename, player_name);
        }
    }

    uade_cleanup_state(state, 0, &temp_wrapper);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo uade_plugin_read_data(void* user_data, RVReadData dest) {
    UadeReplayerData* data = (UadeReplayerData*)user_data;
    RVAudioFormat format = { RVAudioStreamFormat_S16, 2, OUTPUT_SAMPLE_RATE };
    RVReadStatus status = RVReadStatus_Ok;

    if (!data->state) {
        return (RVReadInfo) { format, 0, RVReadStatus_Error};
    }

    // Calculate how many S16 stereo frames fit in the output buffer
    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(int16_t) * 2);

    // UADE produces stereo S16 samples directly to output buffer (4 bytes per frame)
    ssize_t bytes_read = uade_read(static_cast<int16_t*>(dest.channels_output), max_frames * 4, data->state);

    if (bytes_read < 0) {
        status = RVReadStatus_Error;
        bytes_read = 0;
    } else if (bytes_read == 0) {
        // Check notification for end of song or subsong change
        struct uade_notification notification;
        while (uade_read_notification(&notification, data->state)) {
            if (notification.type == UADE_NOTIFICATION_SONG_END) {
                status = RVReadStatus_Finished;
                rv_debug("UADE: Song ended (happy: %d, stopnow: %d)", notification.song_end.happy,
                         notification.song_end.stopnow);
                uade_cleanup_notification(&notification);
                break;
            }
            uade_cleanup_notification(&notification);
        }
    }

    uint32_t frames_read = (uint32_t)(bytes_read / 4);
    return (RVReadInfo) { format, frames_read, status};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t uade_plugin_seek(void* user_data, int64_t ms) {
    UadeReplayerData* data = (UadeReplayerData*)user_data;

    if (!data->state) {
        return -1;
    }

    // UADE has limited seeking support via uade_seek
    if (uade_seek(UADE_SEEK_SUBSONG_RELATIVE, (double)ms / 1000.0, -1, data->state) == 0) {
        return ms;
    }

    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int uade_plugin_metadata(const char* url, const RVService* service_api) {
    const RVMetadata* metadata_api = RVService_get_metadata(service_api, RV_METADATA_API_VERSION);

    // Create state with spawn=1 for full playback capability (needed to get all metadata)
    ThreadWrapper thread_wrapper;
    struct uade_state* state = create_uade_state(1, &thread_wrapper);
    if (!state) {
        rv_error("UADE: Failed to create state for metadata extraction");
        return -1;
    }

    // Start playback to get metadata
    if (uade_play(url, -1, state) != 1) {
        uade_stop(state);
        uade_cleanup_state(state, 1, &thread_wrapper);
        return -1;
    }

    RVMetadataId index = RVMetadata_create_url(metadata_api, url);

    const struct uade_song_info* song_info = uade_get_song_info(state);
    if (song_info) {
        // Set title from module name or filename
        if (song_info->modulename[0] != '\0') {
            RVMetadata_set_tag(metadata_api, index, RV_METADATA_TITLE_TAG, song_info->modulename);
        }

        // Set song type from player name
        if (song_info->playername[0] != '\0') {
            char songtype[128];
            snprintf(songtype, sizeof(songtype), "UADE (%s)", song_info->playername);
            RVMetadata_set_tag(metadata_api, index, RV_METADATA_SONGTYPE_TAG, songtype);
        } else {
            RVMetadata_set_tag(metadata_api, index, RV_METADATA_SONGTYPE_TAG, "UADE (Amiga)");
        }

        // Set duration if available
        if (song_info->duration > 0) {
            RVMetadata_set_tag_f64(metadata_api, index, RV_METADATA_LENGTH_TAG, song_info->duration);
        }

        // Add subsongs if more than one
        int sub_min = song_info->subsongs.min;
        int sub_max = song_info->subsongs.max;
        int num_subsongs = sub_max - sub_min + 1;

        if (num_subsongs > 1) {
            char title[256];
            if (song_info->modulename[0] != '\0') {
                strncpy(title, song_info->modulename, sizeof(title) - 1);
                title[sizeof(title) - 1] = '\0';
            } else {
                // Extract filename from URL
                const char* filename = strrchr(url, '/');
                if (filename) {
                    filename++;
                } else {
                    filename = url;
                }
                strncpy(title, filename, sizeof(title) - 1);
                title[sizeof(title) - 1] = '\0';
            }

            for (int i = sub_min; i <= sub_max; i++) {
                char subsong_name[512];
                snprintf(subsong_name, sizeof(subsong_name), "%s (%d/%d)", title, i, sub_max);
                RVMetadata_add_subsong(metadata_api, index, (uint32_t)i, subsong_name, 0.0f);
            }
        }
    }

    uade_stop(state);
    uade_cleanup_state(state, 1, &thread_wrapper);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void uade_plugin_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void uade_plugin_static_init(const RVService* service_api) {
    g_rv_log = RVService_get_log(service_api, RV_LOG_API_VERSION);

    // Set base directory for UADE data files
    // The plugin expects the uade data directory to be alongside the plugin
    // Default to ~/.replay2/system/cores/music_player/playback_plugins/uade_data
    const char* home = getenv("HOME");
    if (home) {
        snprintf(g_uade_base_dir, sizeof(g_uade_base_dir),
                 "%s/.replay2/system/cores/music_player/playback_plugins/uade_data", home);
    } else {
        strncpy(g_uade_base_dir, "uade_data", sizeof(g_uade_base_dir) - 1);
    }

    rv_info("UADE: Initialized with base directory: %s", g_uade_base_dir);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t uade_plugin_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    UadeReplayerData* data = (UadeReplayerData*)user_data;
    if (data == nullptr || data->state == nullptr || buffer == nullptr) {
        return 0;
    }

    if (!data->scope_enabled) {
        audio_scope_enable(1);
        data->scope_enabled = true;
    }

    return audio_scope_get_data(channel, buffer, num_samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t uade_plugin_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    (void)user_data;
    static const char* s_names[] = { "Paula 0", "Paula 1", "Paula 2", "Paula 3" };
    uint32_t count = 4;
    if (count > max_channels)
        count = max_channels;
    for (uint32_t i = 0; i < count; i++)
        names[i] = s_names[i];
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_uade_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "uade",
    "0.0.1",
    "UADE 3.0", // Library version
    uade_plugin_probe_can_play,
    uade_supported_extensions,
    uade_plugin_create,
    uade_plugin_destroy,
    uade_plugin_event,
    uade_plugin_open,
    uade_plugin_close,
    uade_plugin_read_data,
    uade_plugin_seek,
    uade_plugin_metadata,
    uade_plugin_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    uade_plugin_get_scope_data,
    nullptr, // static_destroy
    uade_plugin_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_uade_plugin;
}
