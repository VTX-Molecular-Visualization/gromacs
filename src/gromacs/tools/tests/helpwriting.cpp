/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2020, by the GROMACS development team, led by
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
 * \brief
 * This implements tests on tool help writing. Based on mdrun test version.
 *
 * \author Mark Abraham <mark.j.abraham@gmail.com>
 * \author Paul Bauer <paul.bauer.q@gmail.com>
 */
#include "gmxpre.h"

#include <memory>

#include "gromacs/commandline/cmdlinehelpcontext.h"
#include "gromacs/commandline/cmdlinemodule.h"
#include "gromacs/commandline/cmdlineoptionsmodule.h"
#include "gromacs/tools/convert_tpr.h"
#include "gromacs/tools/dump.h"
#include "gromacs/tools/report_methods.h"
#include "gromacs/utility/stringstream.h"
#include "gromacs/utility/textwriter.h"

#include "testutils/cmdlinetest.h"
#include "testutils/refdata.h"

namespace gmx
{
namespace test
{
namespace
{

TEST(LegacyHelpwritingTest, ConvertTprWritesHelp)
{
    // Make a stream to which we want gmx convert-tpr -h to write the help.
    StringOutputStream outputStream;
    TextWriter         writer(&outputStream);

    // Use that stream to set up a global help context. Legacy tools
    // like convert-tpr call parse_common_args, which recognizes the
    // existence of a global help context. That context triggers the
    // writing of help and a fast exit of the tool.
    HelpLinks*                   links = nullptr;
    CommandLineHelpContext       context(&writer, eHelpOutputFormat_Console, links, "dummy");
    GlobalCommandLineHelpContext global(context);

    // Call convert-tpr to get the help printed to the stream
    CommandLine caller;
    caller.append("convert-tpr");
    caller.append("-h");
    ASSERT_EQ(0, gmx_convert_tpr(caller.argc(), caller.argv()));

    // Check whether the stream matches the reference copy.
    TestReferenceData    refData;
    TestReferenceChecker checker(refData.rootChecker());
    checker.checkString(outputStream.toString(), "Help string");
};


class HelpwritingTest : public gmx::test::CommandLineTestBase
{
public:
    void runTest(gmx::ICommandLineModule* module) { testWriteHelp(module); }
};

TEST_F(HelpwritingTest, DumpWritesHelp)
{
    const std::unique_ptr<gmx::ICommandLineModule> module(
            gmx::ICommandLineOptionsModule::createModule("dump", "Dummy Info", DumpInfo::create()));
    runTest(module.get());
};

TEST_F(HelpwritingTest, ReportMethodsWritesHelp)
{
    const std::unique_ptr<gmx::ICommandLineModule> module(gmx::ICommandLineOptionsModule::createModule(
            "report-methods", "Dummy Info", ReportMethodsInfo::create()));
    runTest(module.get());
};

} // namespace
} // namespace test
} // namespace gmx
