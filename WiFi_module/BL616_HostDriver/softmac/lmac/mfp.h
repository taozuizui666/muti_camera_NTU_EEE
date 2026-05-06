#ifndef _MFP_H_
#define _MFP_H_

/// list of MFP protection
enum mfp_protection
{
    /// no protection needed
    MFP_NO_PROT,
    /// need unicast protection
    MFP_UNICAST_PROT,
    /// need multicast protection (i.e. MGMT MIC)
    MFP_MULTICAST_PROT,
};


#endif

