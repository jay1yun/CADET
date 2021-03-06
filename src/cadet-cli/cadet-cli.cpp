// =============================================================================
//  CADET - The Chromatography Analysis and Design Toolkit
//  
//  Copyright © 2008-2016: The CADET Authors
//            Please see the AUTHORS and CONTRIBUTORS file.
//  
//  All rights reserved. This program and the accompanying materials
//  are made available under the terms of the GNU Public License v3.0 (or, at
//  your option, any later version) which accompanies this distribution, and
//  is available at http://www.gnu.org/licenses/gpl.html
// =============================================================================

#include "cadet/cadet.hpp"
#include "io/hdf5/HDF5Reader.hpp"
#include "io/hdf5/HDF5Writer.hpp"
#include "io/xml/XMLReader.hpp"
#include "io/xml/XMLWriter.hpp"

#include <tclap/CmdLine.h>
#include "common/TclapUtils.hpp"

#include "Logging.hpp"

#include "common/CompilerSpecific.hpp"
#include "common/ParameterProviderImpl.hpp"
#include "common/Driver.hpp"

#ifdef CADET_BENCHMARK_MODE
	#include "common/Timer.hpp"
#endif

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cctype>

#ifndef CADET_LOGGING_DISABLE
	template <>
	cadet::LogLevel cadet::log::RuntimeFilteringLogger<cadet::log::GlobalLogger>::_minLvl = cadet::LogLevel::Trace;
#endif

namespace
{
	inline void setLocalLogLevel(cadet::LogLevel newLL)
	{
#ifndef CADET_LOGGING_DISABLE
		cadet::log::RuntimeFilteringLogger<cadet::log::GlobalLogger>::level(newLL);
#endif
	}
}

class LogReceiver : public cadet::ILogReceiver
{
public:
	LogReceiver() { }

	virtual void message(const char* file, const char* func, const unsigned int line, cadet::LogLevel lvl, const char* lvlStr, const char* message)
	{
		std::cout << '[' << lvlStr << ": " << func << "::" << line << "] " << message << std::flush;
	}
};

#ifdef CADET_BENCHMARK_MODE
	/**
	 * @brief Scope class that starts a timer on construction and stops it on destruction
	 */
	class BenchScope
	{
	public:
		BenchScope() : _timer() { _timer.start(); }
		~BenchScope() CADET_NOEXCEPT
		{
			const double t = _timer.stop();
			std::cout << "Total elapsed time: " << t << " sec" << std::endl;
		}
	private:
		cadet::Timer _timer;
	};
#endif

// Command line parsing support for cadet::LogLevel type
namespace TCLAP 
{
	template<>
	struct ArgTraits<cadet::LogLevel>
	{
		typedef StringLike ValueCategory;
	};

	template<>
	void SetString<cadet::LogLevel>(cadet::LogLevel& v, const std::string& s)
	{
		if (std::isdigit(s[0]))
		{
			const unsigned int lvl = std::stoul(s);
			if (lvl > static_cast<typename std::underlying_type<cadet::LogLevel>::type>(cadet::LogLevel::Trace))
				throw TCLAP::ArgParseException("Couldn't convert '" + s + "' to a valid log level");

			v = static_cast<cadet::LogLevel>(lvl);
		}
		else
		{
			const cadet::LogLevel lvl = cadet::to_loglevel(s);
			if (cadet::to_string(lvl) != s)
				throw TCLAP::ArgParseException("Couldn't convert '" + s + "' to a valid log level");

			v = lvl;
		}
	}
}

template <class Reader_t, class Writer_t>
void run(const std::string& inFileName, const std::string& outFileName)
{
	cadet::Driver drv;
	
	{
		Reader_t rd;
		rd.openFile(inFileName, "r");

		cadet::ParameterProviderImpl<Reader_t> pp(rd);
		drv.configure(pp);

		rd.closeFile();
	}

	drv.run();

	Writer_t writer;
	if (inFileName == outFileName)
		writer.openFile(outFileName, "rw");
	else
		writer.openFile(outFileName, "co");

	drv.write(writer);
	writer.closeFile();

#ifdef CADET_BENCHMARK_MODE
	// Write timings in JSON format

	// First, timings of the ModelSystem
	std::cout << "{\n\"ModelSystem\":\n\t{\n";
	const std::vector<double> sysTiming = drv.model()->benchmarkTimings();
	char const* const* sysDesc = drv.model()->benchmarkDescriptions();

	for (unsigned int i = 0; i < sysTiming.size()-1; ++i)
		std::cout << "\t\t\"" << sysDesc[i] << "\": " << sysTiming[i] << ",\n";

	std::cout << "\t\t\"" << sysDesc[sysTiming.size()-1] << "\": " << sysTiming[sysTiming.size()-1] << "\n\t}";

	// Then, timings for all unit operations
	for (unsigned int j = 0; j < drv.model()->numModels(); ++j)
	{
		// Skip unit operations that do not provide timings
		cadet::IModel const* const m = drv.model()->getModel(j);
		if (!m->benchmarkDescriptions())
			continue;

		std::cout << ",\n\"" << m->unitOperationName() << static_cast<int>(m->unitOperationId()) << "\":\n\t{\n";
		const std::vector<double> grmTiming = m->benchmarkTimings();
		char const* const* grmDesc = m->benchmarkDescriptions();

		for (unsigned int i = 0; i < grmTiming.size()-1; ++i)
			std::cout << "\t\t\"" << grmDesc[i] << "\": " << grmTiming[i] << ",\n";

		std::cout << "\t\t\"" << grmDesc[grmTiming.size()-1] << "\": " << grmTiming[grmTiming.size()-1] << "\n\t}";
	}
	std::cout << "\n}" << std::endl;
#endif
}


int main(int argc, char** argv)
{	
#ifdef CADET_BENCHMARK_MODE
	// Benchmark the whole program from start to finish
	BenchScope bsTotalTime;
#endif

	// Program options
	std::string inFileName = "";
	std::string outFileName = "";
	cadet::LogLevel logLevel = cadet::LogLevel::Trace;

	try
	{
		TCLAP::CustomOutput customOut("cadet-cli");
		TCLAP::CmdLine cmd("Simulates a chromatography setup using CADET", ' ', "1.0");
		cmd.setOutput(&customOut);

		cmd >> (new TCLAP::ValueArg<cadet::LogLevel>("L", "loglevel", "Set the log level", false, cadet::LogLevel::Trace, "LogLevel"))->storeIn(&logLevel);
		cmd >> (new TCLAP::UnlabeledValueArg<std::string>("input", "Input file", true, "", "File"))->storeIn(&inFileName);
		cmd >> (new TCLAP::UnlabeledValueArg<std::string>("output", "Output file (defaults to input file)", false, "", "File"))->storeIn(&outFileName);

		cmd.parse(argc, argv);
	}
	catch (const TCLAP::ArgException &e)
	{
		std::cerr << "ERROR: " << e.error() << " for argument " << e.argId() << std::endl;
		return 1;
	}

	// If no dedicated output filename was given, assume output = input file
	if (outFileName.empty())
		outFileName = inFileName;

	std::cout << std::scientific << std::setprecision(std::numeric_limits<double>::digits10 + 1);

	// Set LogLevel in library and locally
	LogReceiver lr;
	cadetSetLogReceiver(&lr);
	cadetSetLogLevel(static_cast<typename std::underlying_type<cadet::LogLevel>::type>(logLevel));
	setLocalLogLevel(logLevel);

	// Obtain file extensions for selecting corresponding reader and writer
	const std::size_t dotPosIn = inFileName.find_last_of('.');
	if (dotPosIn == std::string::npos)
	{
		std::cerr << "Could not deduce input filetype due to missing extension: " << inFileName << std::endl;
		return 2;
	}

	const std::size_t dotPosOut = outFileName.find_last_of('.');
	if (dotPosOut == std::string::npos)
	{
		std::cerr << "Could not deduce output filetype due to missing extension: " << outFileName << std::endl;
		return 2;
	}

	const std::string fileExtIn = inFileName.substr(dotPosIn+1);
	const std::string fileExtOut = outFileName.substr(dotPosOut+1);

	try
	{
		if (cadet::util::caseInsensitiveEquals(fileExtIn, "h5"))
		{
			if (cadet::util::caseInsensitiveEquals(fileExtOut, "h5"))
			{
				run<cadet::io::HDF5Reader, cadet::io::HDF5Writer>(inFileName, outFileName);
			}
			else if (cadet::util::caseInsensitiveEquals(fileExtOut, "xml"))
			{
				run<cadet::io::HDF5Reader, cadet::io::XMLWriter>(inFileName, outFileName);
			}
			else
			{
				std::cerr << "Output file format ('." << fileExtOut << "') not supported" << std::endl;
				return 2;
			}
		}
		else if (cadet::util::caseInsensitiveEquals(fileExtIn, "xml"))
		{
			if (cadet::util::caseInsensitiveEquals(fileExtOut, "xml"))
			{
				run<cadet::io::XMLReader, cadet::io::XMLWriter>(inFileName, outFileName);
			}
			else if (cadet::util::caseInsensitiveEquals(fileExtOut, "h5"))
			{
				run<cadet::io::XMLReader, cadet::io::HDF5Writer>(inFileName, outFileName);
			}
			else
			{
				std::cerr << "Output file format ('." << fileExtOut << "') not supported" << std::endl;
				return 2;
			}
		}
		else
		{
			std::cerr << "Input file format ('." << fileExtIn << "') not supported" << std::endl;
			return 2;
		}
	}
	catch (const cadet::io::IOException& e)
	{
		std::cerr << "IO ERROR: " << e.what() << std::endl;
		return 2;
	}
	catch (const cadet::IntegrationException& e)
	{
		std::cerr << "SOLVER ERROR: " << e.what() << std::endl;
		return 3;
	}
	catch (const std::exception& e)
	{
		std::cerr << "ERROR: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
