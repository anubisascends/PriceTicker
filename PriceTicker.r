#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "AE_General.r"

resource 'PiPL' (16000) {
    {   /* array properties: 11 elements */

        /* [1] */
        Kind {
            AEEffect
        },
        /* [2] */
        Name {
            "Card Price Ticker"
        },
        /* [3] */
        Category {
            "SDK"
        },

        /* [4] */
#ifdef AE_OS_WIN
        CodeWin64X86 {"EffectMain"},
#else
        CodeMacARM64 {"EffectMain"},
        CodeMacIntel64 {"EffectMain"},
#endif

        /* [5] */
        AE_PiPL_Version {
            2,
            0
        },
        /* [6] */
        AE_Effect_Spec_Version {
            PF_PLUG_IN_VERSION,
            PF_PLUG_IN_SUBVERS
        },
        /* [7] */
        AE_Effect_Version {
            524288      /* 1.0, PF_Stage_DEVELOP */
        },
        /* [8] */
        AE_Effect_Info_Flags {
            0
        },
        /* [9] */
        /* Must match GlobalSetup's out_flags:
           PF_OutFlag_SEQUENCE_DATA_NEEDS_FLATTENING (0x10)
         | PF_OutFlag_USE_OUTPUT_EXTENT              (0x40) = 0x50 */
        AE_Effect_Global_OutFlags {
            0x50
        },
        AE_Effect_Global_OutFlags_2 {
            0x0
        },
        /* [10] */
        AE_Effect_Match_Name {
            "SDK Card Price Ticker"
        },
        /* [11] */
        AE_Reserved_Info {
            8
        }
    }
};
