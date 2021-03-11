/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2012,2013,2014,2015,2016 by the GROMACS development team.
 * Copyright (c) 2017,2018,2019,2020,2021, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
/*! \internal \file
 *  \brief Define common implementation of nbnxm_gpu_data_mgmt.h
 *
 *  \author Anca Hamuraru <anca@streamcomputing.eu>
 *  \author Dimitrios Karkoulis <dimitris.karkoulis@gmail.com>
 *  \author Teemu Virolainen <teemu@streamcomputing.eu>
 *  \author Szilárd Páll <pall.szilard@gmail.com>
 *  \author Artem Zhmurov <zhmurov@gmail.com>
 *
 *  \ingroup module_nbnxm
 */
#include "gmxpre.h"

#include "config.h"

#if GMX_GPU_CUDA
#    include "cuda/nbnxm_cuda_types.h"
#endif

#if GMX_GPU_OPENCL
#    include "opencl/nbnxm_ocl_types.h"
#endif

#if GMX_GPU_SYCL
#    include "sycl/nbnxm_sycl_types.h"
#endif

#include "nbnxm_gpu_data_mgmt.h"

#include "gromacs/hardware/device_information.h"
#include "gromacs/mdtypes/interaction_const.h"
#include "gromacs/nbnxm/gpu_common_utils.h"
#include "gromacs/nbnxm/gpu_data_mgmt.h"
#include "gromacs/timing/gpu_timing.h"
#include "gromacs/utility/cstringutil.h"
#include "gromacs/utility/exceptions.h"
#include "gromacs/utility/fatalerror.h"

#include "nbnxm_gpu.h"
#include "pairlistsets.h"

namespace Nbnxm
{

void init_ewald_coulomb_force_table(const EwaldCorrectionTables& tables,
                                    NBParamGpu*                  nbp,
                                    const DeviceContext&         deviceContext)
{
    if (nbp->coulomb_tab)
    {
        destroyParamLookupTable(&nbp->coulomb_tab, nbp->coulomb_tab_texobj);
    }

    nbp->coulomb_tab_scale = tables.scale;
    initParamLookupTable(
            &nbp->coulomb_tab, &nbp->coulomb_tab_texobj, tables.tableF.data(), tables.tableF.size(), deviceContext);
}

enum ElecType nbnxn_gpu_pick_ewald_kernel_type(const interaction_const_t& ic,
                                               const DeviceInformation gmx_unused& deviceInfo)
{
    bool bTwinCut = (ic.rcoulomb != ic.rvdw);

    /* Benchmarking/development environment variables to force the use of
       analytical or tabulated Ewald kernel. */
    const bool forceAnalyticalEwald = (getenv("GMX_GPU_NB_ANA_EWALD") != nullptr);
    const bool forceTabulatedEwald  = (getenv("GMX_GPU_NB_TAB_EWALD") != nullptr);
    const bool forceTwinCutoffEwald = (getenv("GMX_GPU_NB_EWALD_TWINCUT") != nullptr);

    if (forceAnalyticalEwald && forceTabulatedEwald)
    {
        gmx_incons(
                "Both analytical and tabulated Ewald GPU non-bonded kernels "
                "requested through environment variables.");
    }

    /* By default, use analytical Ewald except with CUDA on NVIDIA CC 7.0 and 8.0.
     */
    const bool c_useTabulatedEwaldDefault =
#if GMX_GPU_CUDA
            (deviceInfo.prop.major == 7 && deviceInfo.prop.minor == 0)
            || (deviceInfo.prop.major == 8 && deviceInfo.prop.minor == 0);
#else
            false;
#endif
    bool bUseAnalyticalEwald = !c_useTabulatedEwaldDefault;
    if (forceAnalyticalEwald)
    {
        bUseAnalyticalEwald = true;
        if (debug)
        {
            fprintf(debug, "Using analytical Ewald GPU kernels\n");
        }
    }
    else if (forceTabulatedEwald)
    {
        bUseAnalyticalEwald = false;

        if (debug)
        {
            fprintf(debug, "Using tabulated Ewald GPU kernels\n");
        }
    }

    /* Use twin cut-off kernels if requested by bTwinCut or the env. var.
       forces it (use it for debugging/benchmarking only). */
    if (!bTwinCut && !forceTwinCutoffEwald)
    {
        return bUseAnalyticalEwald ? ElecType::EwaldAna : ElecType::EwaldTab;
    }
    else
    {
        return bUseAnalyticalEwald ? ElecType::EwaldAnaTwin : ElecType::EwaldTabTwin;
    }
}

void set_cutoff_parameters(NBParamGpu* nbp, const interaction_const_t* ic, const PairlistParams& listParams)
{
    nbp->ewald_beta        = ic->ewaldcoeff_q;
    nbp->sh_ewald          = ic->sh_ewald;
    nbp->epsfac            = ic->epsfac;
    nbp->two_k_rf          = 2.0 * ic->reactionFieldCoefficient;
    nbp->c_rf              = ic->reactionFieldShift;
    nbp->rvdw_sq           = ic->rvdw * ic->rvdw;
    nbp->rcoulomb_sq       = ic->rcoulomb * ic->rcoulomb;
    nbp->rlistOuter_sq     = listParams.rlistOuter * listParams.rlistOuter;
    nbp->rlistInner_sq     = listParams.rlistInner * listParams.rlistInner;
    nbp->useDynamicPruning = listParams.useDynamicPruning;

    nbp->sh_lj_ewald   = ic->sh_lj_ewald;
    nbp->ewaldcoeff_lj = ic->ewaldcoeff_lj;

    nbp->rvdw_switch      = ic->rvdw_switch;
    nbp->dispersion_shift = ic->dispersion_shift;
    nbp->repulsion_shift  = ic->repulsion_shift;
    nbp->vdw_switch       = ic->vdw_switch;
}

void gpu_pme_loadbal_update_param(const nonbonded_verlet_t* nbv, const interaction_const_t* ic)
{
    if (!nbv || !nbv->useGpu())
    {
        return;
    }
    NbnxmGpu*   nb  = nbv->gpu_nbv;
    NBParamGpu* nbp = nb->nbparam;

    set_cutoff_parameters(nbp, ic, nbv->pairlistSets().params());

    nbp->elecType = nbnxn_gpu_pick_ewald_kernel_type(*ic, nb->deviceContext_->deviceInfo());

    GMX_RELEASE_ASSERT(ic->coulombEwaldTables, "Need valid Coulomb Ewald correction tables");
    init_ewald_coulomb_force_table(*ic->coulombEwaldTables, nbp, *nb->deviceContext_);
}

void init_plist(gpu_plist* pl)
{
    /* initialize to nullptr pointers to data that is not allocated here and will
       need reallocation in nbnxn_gpu_init_pairlist */
    pl->sci   = nullptr;
    pl->cj4   = nullptr;
    pl->imask = nullptr;
    pl->excl  = nullptr;

    /* size -1 indicates that the respective array hasn't been initialized yet */
    pl->na_c                   = -1;
    pl->nsci                   = -1;
    pl->sci_nalloc             = -1;
    pl->ncj4                   = -1;
    pl->cj4_nalloc             = -1;
    pl->nimask                 = -1;
    pl->imask_nalloc           = -1;
    pl->nexcl                  = -1;
    pl->excl_nalloc            = -1;
    pl->haveFreshList          = false;
    pl->rollingPruningNumParts = 0;
    pl->rollingPruningPart     = 0;
}

void init_timings(gmx_wallclock_gpu_nbnxn_t* t)
{
    t->nb_h2d_t = 0.0;
    t->nb_d2h_t = 0.0;
    t->nb_c     = 0;
    t->pl_h2d_t = 0.0;
    t->pl_h2d_c = 0;
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            t->ktime[i][j].t = 0.0;
            t->ktime[i][j].c = 0;
        }
    }
    t->pruneTime.c        = 0;
    t->pruneTime.t        = 0.0;
    t->dynamicPruneTime.c = 0;
    t->dynamicPruneTime.t = 0.0;
}

//! This function is documented in the header file
void gpu_init_pairlist(NbnxmGpu* nb, const NbnxnPairlistGpu* h_plist, const InteractionLocality iloc)
{
    char sbuf[STRLEN];
    // Timing accumulation should happen only if there was work to do
    // because getLastRangeTime() gets skipped with empty lists later
    // which leads to the counter not being reset.
    bool                bDoTime      = (nb->bDoTime && !h_plist->sci.empty());
    const DeviceStream& deviceStream = *nb->deviceStreams[iloc];
    gpu_plist*          d_plist      = nb->plist[iloc];

    if (d_plist->na_c < 0)
    {
        d_plist->na_c = h_plist->na_ci;
    }
    else
    {
        if (d_plist->na_c != h_plist->na_ci)
        {
            sprintf(sbuf,
                    "In init_plist: the #atoms per cell has changed (from %d to %d)",
                    d_plist->na_c,
                    h_plist->na_ci);
            gmx_incons(sbuf);
        }
    }

    GpuTimers::Interaction& iTimers = nb->timers->interaction[iloc];

    if (bDoTime)
    {
        iTimers.pl_h2d.openTimingRegion(deviceStream);
        iTimers.didPairlistH2D = true;
    }

    // TODO most of this function is same in CUDA and OpenCL, move into the header
    const DeviceContext& deviceContext = *nb->deviceContext_;

    reallocateDeviceBuffer(
            &d_plist->sci, h_plist->sci.size(), &d_plist->nsci, &d_plist->sci_nalloc, deviceContext);
    copyToDeviceBuffer(&d_plist->sci,
                       h_plist->sci.data(),
                       0,
                       h_plist->sci.size(),
                       deviceStream,
                       GpuApiCallBehavior::Async,
                       bDoTime ? iTimers.pl_h2d.fetchNextEvent() : nullptr);

    reallocateDeviceBuffer(
            &d_plist->cj4, h_plist->cj4.size(), &d_plist->ncj4, &d_plist->cj4_nalloc, deviceContext);
    copyToDeviceBuffer(&d_plist->cj4,
                       h_plist->cj4.data(),
                       0,
                       h_plist->cj4.size(),
                       deviceStream,
                       GpuApiCallBehavior::Async,
                       bDoTime ? iTimers.pl_h2d.fetchNextEvent() : nullptr);

    reallocateDeviceBuffer(&d_plist->imask,
                           h_plist->cj4.size() * c_nbnxnGpuClusterpairSplit,
                           &d_plist->nimask,
                           &d_plist->imask_nalloc,
                           deviceContext);

    reallocateDeviceBuffer(
            &d_plist->excl, h_plist->excl.size(), &d_plist->nexcl, &d_plist->excl_nalloc, deviceContext);
    copyToDeviceBuffer(&d_plist->excl,
                       h_plist->excl.data(),
                       0,
                       h_plist->excl.size(),
                       deviceStream,
                       GpuApiCallBehavior::Async,
                       bDoTime ? iTimers.pl_h2d.fetchNextEvent() : nullptr);

    if (bDoTime)
    {
        iTimers.pl_h2d.closeTimingRegion(deviceStream);
    }

    /* need to prune the pair list during the next step */
    d_plist->haveFreshList = true;
}

//! This function is documented in the header file
gmx_wallclock_gpu_nbnxn_t* gpu_get_timings(NbnxmGpu* nb)
{
    return (nb != nullptr && nb->bDoTime) ? nb->timings : nullptr;
}

//! This function is documented in the header file
void gpu_reset_timings(nonbonded_verlet_t* nbv)
{
    if (nbv->gpu_nbv && nbv->gpu_nbv->bDoTime)
    {
        init_timings(nbv->gpu_nbv->timings);
    }
}

bool gpu_is_kernel_ewald_analytical(const NbnxmGpu* nb)
{
    return ((nb->nbparam->elecType == ElecType::EwaldAna)
            || (nb->nbparam->elecType == ElecType::EwaldAnaTwin));
}

enum ElecType nbnxmGpuPickElectrostaticsKernelType(const interaction_const_t* ic,
                                                   const DeviceInformation&   deviceInfo)
{
    if (ic->eeltype == CoulombInteractionType::Cut)
    {
        return ElecType::Cut;
    }
    else if (EEL_RF(ic->eeltype))
    {
        return ElecType::RF;
    }
    else if ((EEL_PME(ic->eeltype) || ic->eeltype == CoulombInteractionType::Ewald))
    {
        return nbnxn_gpu_pick_ewald_kernel_type(*ic, deviceInfo);
    }
    else
    {
        /* Shouldn't happen, as this is checked when choosing Verlet-scheme */
        GMX_THROW(gmx::InconsistentInputError(
                gmx::formatString("The requested electrostatics type %s is not implemented in "
                                  "the GPU accelerated kernels!",
                                  enumValueToString(ic->eeltype))));
    }
}


enum VdwType nbnxmGpuPickVdwKernelType(const interaction_const_t* ic, LJCombinationRule ljCombinationRule)
{
    if (ic->vdwtype == VanDerWaalsType::Cut)
    {
        switch (ic->vdw_modifier)
        {
            case InteractionModifiers::None:
            case InteractionModifiers::PotShift:
                switch (ljCombinationRule)
                {
                    case LJCombinationRule::None: return VdwType::Cut;
                    case LJCombinationRule::Geometric: return VdwType::CutCombGeom;
                    case LJCombinationRule::LorentzBerthelot: return VdwType::CutCombLB;
                    default:
                        GMX_THROW(gmx::InconsistentInputError(gmx::formatString(
                                "The requested LJ combination rule %s is not implemented in "
                                "the GPU accelerated kernels!",
                                enumValueToString(ljCombinationRule))));
                }
            case InteractionModifiers::ForceSwitch: return VdwType::FSwitch;
            case InteractionModifiers::PotSwitch: return VdwType::PSwitch;
            default:
                GMX_THROW(gmx::InconsistentInputError(
                        gmx::formatString("The requested VdW interaction modifier %s is not "
                                          "implemented in the GPU accelerated kernels!",
                                          enumValueToString(ic->vdw_modifier))));
        }
    }
    else if (ic->vdwtype == VanDerWaalsType::Pme)
    {
        if (ic->ljpme_comb_rule == LongRangeVdW::Geom)
        {
            assert(ljCombinationRule == LJCombinationRule::Geometric);
            return VdwType::EwaldGeom;
        }
        else
        {
            assert(ljCombinationRule == LJCombinationRule::LorentzBerthelot);
            return VdwType::EwaldLB;
        }
    }
    else
    {
        GMX_THROW(gmx::InconsistentInputError(gmx::formatString(
                "The requested VdW type %s is not implemented in the GPU accelerated kernels!",
                enumValueToString(ic->vdwtype))));
    }
}

void setupGpuShortRangeWork(NbnxmGpu* nb, const gmx::GpuBonded* gpuBonded, const gmx::InteractionLocality iLocality)
{
    GMX_ASSERT(nb, "Need a valid nbnxn_gpu object");

    // There is short-range work if the pair list for the provided
    // interaction locality contains entries or if there is any
    // bonded work (as this is not split into local/nonlocal).
    nb->haveWork[iLocality] = ((nb->plist[iLocality]->nsci != 0)
                               || (gpuBonded != nullptr && gpuBonded->haveInteractions()));
}

bool haveGpuShortRangeWork(const NbnxmGpu* nb, const gmx::AtomLocality aLocality)
{
    GMX_ASSERT(nb, "Need a valid nbnxn_gpu object");

    return haveGpuShortRangeWork(*nb, gpuAtomToInteractionLocality(aLocality));
}

inline void issueClFlushInStream(const DeviceStream& gmx_unused deviceStream)
{
#if GMX_GPU_OPENCL
    /* Based on the v1.2 section 5.13 of the OpenCL spec, a flush is needed
     * in the stream after marking an event in it in order to be able to sync with
     * the event from another stream.
     */
    cl_int cl_error = clFlush(deviceStream.stream());
    if (cl_error != CL_SUCCESS)
    {
        GMX_THROW(gmx::InternalError("clFlush failed: " + ocl_get_error_string(cl_error)));
    }
#endif
}

void nbnxnInsertNonlocalGpuDependency(NbnxmGpu* nb, const InteractionLocality interactionLocality)
{
    const DeviceStream& deviceStream = *nb->deviceStreams[interactionLocality];

    /* When we get here all misc operations issued in the local stream as well as
       the local xq H2D are done,
       so we record that in the local stream and wait for it in the nonlocal one.
       This wait needs to precede any PP tasks, bonded or nonbonded, that may
       compute on interactions between local and nonlocal atoms.
     */
    if (nb->bUseTwoStreams)
    {
        if (interactionLocality == InteractionLocality::Local)
        {
            nb->misc_ops_and_local_H2D_done.markEvent(deviceStream);
            issueClFlushInStream(deviceStream);
        }
        else
        {
            nb->misc_ops_and_local_H2D_done.enqueueWaitEvent(deviceStream);
        }
    }
}

/*! \brief Launch asynchronously the xq buffer host to device copy. */
void gpu_copy_xq_to_gpu(NbnxmGpu* nb, const nbnxn_atomdata_t* nbatom, const AtomLocality atomLocality)
{
    GMX_ASSERT(nb, "Need a valid nbnxn_gpu object");

    const InteractionLocality iloc = gpuAtomToInteractionLocality(atomLocality);

    NBAtomData*         adat         = nb->atdat;
    gpu_plist*          plist        = nb->plist[iloc];
    Nbnxm::GpuTimers*   timers       = nb->timers;
    const DeviceStream& deviceStream = *nb->deviceStreams[iloc];

    const bool bDoTime = nb->bDoTime;

    /* Don't launch the non-local H2D copy if there is no dependent
       work to do: neither non-local nor other (e.g. bonded) work
       to do that has as input the nbnxn coordaintes.
       Doing the same for the local kernel is more complicated, since the
       local part of the force array also depends on the non-local kernel.
       So to avoid complicating the code and to reduce the risk of bugs,
       we always call the local local x+q copy (and the rest of the local
       work in nbnxn_gpu_launch_kernel().
     */
    if ((iloc == InteractionLocality::NonLocal) && !haveGpuShortRangeWork(*nb, iloc))
    {
        plist->haveFreshList = false;

        // The event is marked for Local interactions unconditionally,
        // so it has to be released here because of the early return
        // for NonLocal interactions.
        nb->misc_ops_and_local_H2D_done.reset();

        return;
    }

    /* local/nonlocal offset and length used for xq and f */
    const auto atomsRange = getGpuAtomRange(adat, atomLocality);

    /* beginning of timed HtoD section */
    if (bDoTime)
    {
        timers->xf[atomLocality].nb_h2d.openTimingRegion(deviceStream);
    }

    /* HtoD x, q */
    GMX_ASSERT(nbatom->XFormat == nbatXYZQ,
               "The coordinates should be in xyzq format to copy to the Float4 device buffer.");
    copyToDeviceBuffer(&adat->xq,
                       reinterpret_cast<const Float4*>(nbatom->x().data()) + atomsRange.begin(),
                       atomsRange.begin(),
                       atomsRange.size(),
                       deviceStream,
                       GpuApiCallBehavior::Async,
                       nullptr);

    if (bDoTime)
    {
        timers->xf[atomLocality].nb_h2d.closeTimingRegion(deviceStream);
    }

    /* When we get here all misc operations issued in the local stream as well as
       the local xq H2D are done,
       so we record that in the local stream and wait for it in the nonlocal one.
       This wait needs to precede any PP tasks, bonded or nonbonded, that may
       compute on interactions between local and nonlocal atoms.
     */
    nbnxnInsertNonlocalGpuDependency(nb, iloc);
}

} // namespace Nbnxm
