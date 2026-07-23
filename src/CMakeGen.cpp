#include "StdAfx.h"
#include "CMakeGen.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <ctime>
#include <sstream>
#include <fstream>

#include <filesystem>

namespace
{
	struct SourceFile
	{
		std::string directory;
		std::string name;
	};

	void FindSourceFiles(const std::string& directory, std::vector<SourceFile>& sourceFiles)
	{
		WIN32_FIND_DATAA findData{};
		HANDLE findHandle = FindFirstFileA((directory + "\\*").c_str(), &findData);
		if (findHandle == INVALID_HANDLE_VALUE)
			return;

		do
		{
			std::string fileName = findData.cFileName;
			if (fileName == "." || fileName == "..")
				continue;

			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				if ((findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
					FindSourceFiles(directory + "\\" + fileName, sourceFiles);
			}
			else if ((fileName.length() >= 2 && fileName.compare(fileName.length() - 2, 2, ".h") == 0)
				|| (fileName.length() >= 4 && fileName.compare(fileName.length() - 4, 4, ".cpp") == 0))
			{
				sourceFiles.push_back({ directory, fileName });
			}
		} while (FindNextFileA(findHandle, &findData));

		FindClose(findHandle);
	}
}

std::map<std::string, std::vector<std::string>> CMakeGen::sources{};
std::map<std::string, std::string> CMakeGen::VSFilterGroups{};

void CMakeGen::GenerateCMakeList(std::string source_dir)
{
	std::vector<SourceFile> sourceFiles;
	FindSourceFiles(source_dir, sourceFiles);

	for (const auto& sourceFile : sourceFiles)
	{
		std::string fullPath = sourceFile.directory;
		std::string fileName = sourceFile.name;
		std::string relativePath = (fullPath == source_dir) ? "" : fullPath.substr(source_dir.length() + 1, fullPath.length() - source_dir.length());
		std::string VSFilter = (relativePath.empty())? "Main" : relativePath;
		std::string SourceGroupName((relativePath != "")? relativePath : "Main");
		SourceGroupName.erase(std::remove(SourceGroupName.begin(), SourceGroupName.end(), '\\'), SourceGroupName.end());

		//Adds double backslash to source group text in order to clean up visual studio internal filters
		auto it = std::find(VSFilter.begin(), VSFilter.end(), '\\');
		while (it != VSFilter.end()) {
			auto it2 = VSFilter.insert(it, '\\');

			it = std::find(it2 + 2, VSFilter.end(), '\\');
		}

		if (VSFilterGroups.count(SourceGroupName) == 0)
			VSFilterGroups[SourceGroupName] = VSFilter;

		// Add this source file to the source group array
		std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
		sources[SourceGroupName].emplace_back(relativePath + ((relativePath.length() > 0) ? "/" : "") + fileName);
	}

	// Now we can traverse our array and build our sources more easily

	// Get our template file data first
	std::string CMakeContent{};

	if (!UTIL::CheckFileExists(source_dir + "/../CMakeListTemplate.txt"))
	{
		std::cout << "[Warning] Missing CMakeList Template! Generating a default template (Cryengine 5.3 basic)!";

		// Get default
		CMakeContent = UTIL::GetTextResourceA(GetModuleHandleA(NULL), IDR_CMAKETEMPLATE);

		// Save externally for user customization
		std::ofstream CMakeFile(source_dir + "/../CMakeListTemplate.txt");
		CMakeFile << CMakeContent;
		CMakeFile.close();
	}
	else
	{
		CMakeContent = UTIL::ReadWholeFile(source_dir + "/../CMakeListTemplate.txt");
	}

	// Generate source strings for each group
	std::string CMakeSourceData{};
	
	for (auto &g : VSFilterGroups)
	{
		CMakeSourceData.append("set (SourceGroup_" + g.first + "\r\n");
		for (auto &s : sources.at(g.first))
		{
			CMakeSourceData.append("\t\"" + s + "\"\r\n");
		}
		CMakeSourceData.append(std::string(")\r\n") 
			+ "source_group(\"" + g.second + "\" FILES ${" + "SourceGroup_" + g.first + "})\r\n\r\n"
		);
	}

	// Generate sources list
	CMakeSourceData.append("set (SOURCE\r\n");
	for (auto &g : VSFilterGroups)
	{
		CMakeSourceData.append("\t${SourceGroup_" + g.first + "}\r\n");
	}
	CMakeSourceData.append(")\r\n");

	// replace token in template
	auto pos = CMakeContent.find("%%CMakeGen_SOURCES%%");
	if (pos == std::string::npos)
	{
		std::cout << "[Error] Could not find replacement token in cmake template file (refresh the template?)!";
		return;
	}

	CMakeContent.replace(pos, 20, CMakeSourceData);

	// Normalize line endings...
	CMakeContent.erase(std::remove(CMakeContent.begin(), CMakeContent.end(), '\r'), CMakeContent.end());

	if (DO_BACKUP)
	{
		// backup old file
		if (UTIL::CheckFileExists(source_dir + "/CMakeLists.txt"))
		{
			// Get a good suffix to make this backup unique
			auto time = std::time(nullptr);
			struct tm timel;
			localtime_s(&timel, &time);
			std::ostringstream timest;
			timest << std::put_time(&timel, "%y%m%d_%H%M%S");
			auto prefix = timest.str();

			// Write the backup file
			std::ofstream CMakeFile(source_dir + "/" + prefix + "_CMakeLists.txt");
			CMakeFile << UTIL::ReadWholeFile(source_dir + "/CMakeLists.txt");
			CMakeFile.close();

			std::cout << "backup saved to " + source_dir + "/" + prefix + "_CMakeLists.txt";
		}
	}

	// write new file
	std::ofstream CMakeFile(source_dir + "/CMakeLists.txt");
	CMakeFile << CMakeContent;
	CMakeFile.close();

	std::cout << "Finished";
}
