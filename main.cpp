//    Copyright (C) 2017 Michał Karol <michal.p.karol@gmail.com>

//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.

//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.

//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#include <openssl/md5.h>
#include <algorithm>
#include <array>
#include <boost/program_options.hpp>
#include <experimental/filesystem>
#include <experimental/string_view>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <thread>

namespace po = boost::program_options;
namespace fs = std::experimental::filesystem;

typedef std::array<u_char, MD5_DIGEST_LENGTH> hash;
typedef std::map<hash, std::set<fs::directory_entry>> duplicate_map;

const int smallCheckSize = 100 * 1024;

hash calculateFileHash(fs::directory_entry file, u_int size, bool full = false) {
  const int bufferSize = 10 * 1024;

  std::ifstream input;
  input.open(file.path().c_str(), std::ios::binary);
  std::array<char, bufferSize> buffer;
  hash hash;

  MD5_CTX md5;
  MD5_Init(&md5);

  u_int readed = 0;
  while (true) {
    input.read(buffer.data(), bufferSize);

    u_int length =
        (readed + input.gcount() > size && !full
         ? size - readed
         : static_cast<u_int>(input.gcount()));

    MD5_Update(&md5, buffer.data(), length);
    readed += length;

    if (input.gcount() <= 0 || (readed == size && !full)) break;
  }

  MD5_Final(hash.data(), &md5);

  return hash;
}

duplicate_map generateDirectoryHashes(fs::directory_entry directory, const std::set<std::string>& ignored) {
  duplicate_map duplicates;

  for (const fs::directory_entry& entry :
       fs::recursive_directory_iterator(directory)) {
    if (fs::is_regular_file(entry) && ignored.find(entry.path().extension()) == ignored.end()) {
      duplicates[calculateFileHash(entry, smallCheckSize)].insert(entry);
    }
  }

  return duplicates;
}
duplicate_map generateSingleFilesHashes(const std::vector<fs::directory_entry>& files, const std::set<std::string>& ignored) {
  duplicate_map duplicates;

  for (const fs::directory_entry& entry : files) {
    if (fs::is_regular_file(entry) && ignored.find(entry.path().extension()) == ignored.end()) {
      duplicates[calculateFileHash(entry, smallCheckSize)].insert(entry);
    }
  }

  return duplicates;
}

duplicate_map findDuplicates(std::string path, bool full, u_int threadNumber, const std::set<std::string>& ignored) {
  duplicate_map results;

  fs::path startingPath(path);

  // Thread number control
  u_int threads = 0;
  std::mutex mutex;

  std::vector<fs::directory_entry> files;
  std::vector<std::future<duplicate_map>> jobs;

  std::function<void(std::function<duplicate_map(void)>)> wait =
          [&mutex, &threads, &jobs, threadNumber]
          (std::function<duplicate_map(void)> function) {
      bool wait = true;
      do {
          mutex.lock();
          if (threads != threadNumber) {
              jobs.push_back(std::async(std::launch::async, function));
              threads++;
              wait = false;
          }
          mutex.unlock();
      } while (wait);
  };

  for (const fs::directory_entry& entry : fs::directory_iterator(startingPath)) {
    if (fs::is_directory(entry)) {
      wait([entry, &threads, &mutex, &ignored]() -> duplicate_map {
        mutex.lock();
        threads--;
        mutex.unlock();
        return generateDirectoryHashes(entry, ignored);
      });
    } else {
      files.push_back(entry);
    }
  }

  u_long singleBlockSize = static_cast<u_long>(std::ceil(static_cast<double>(files.size()) / threadNumber));

  for (u_int i = 0; i < threadNumber && i * singleBlockSize < files.size();i++) {
    std::vector<fs::directory_entry> filesPart(
        files.begin() + static_cast<long>(i * singleBlockSize),
        files.begin() + static_cast<long>(std::min((i + 1) * singleBlockSize, files.size()))
    );

    wait(
        [&threads, &mutex, &ignored, filesPart = std::move(filesPart) ] () -> duplicate_map {
          mutex.lock();
          threads--;
          mutex.unlock();

          return generateSingleFilesHashes(filesPart, ignored);
        });
  }

  // Merge all results
  for (std::future<duplicate_map>& f : jobs) {
    for (const std::pair<hash, std::set<fs::directory_entry>>& tuple : f.get()) {
      std::set<fs::directory_entry> set = results[tuple.first];
      set.insert(tuple.second.begin(), tuple.second.end());
      results[tuple.first] = std::move(set);
    }
  }

  // Keeping only duplicates
  duplicate_map duplicates;
  std::copy_if(results.begin(), results.end(),
               std::inserter(duplicates, duplicates.end()),
               [](const std::pair<hash, std::set<fs::directory_entry>>& tuple) {
                 return tuple.second.size() > 1;
               });

  // Making full check
  if (full) {
      std::cout << "FULL";
      for (const std::pair<hash, std::set<fs::directory_entry>>& tuple : duplicates) {
          for (const fs::directory_entry& entry : tuple.second) {
              hash result = calculateFileHash(entry, 0 , true);
              if (result != tuple.first) {
                  duplicates[tuple.first].erase(entry);
                  duplicates[result].insert(entry);
              }
          }
      }

      duplicate_map duplicatesFull;
      std::copy_if(duplicates.begin(), duplicates.end(),
                   std::inserter(duplicatesFull, duplicatesFull.end()),
                   [](const std::pair<hash, std::set<fs::directory_entry>>& tuple) {
                     return tuple.second.size() > 1;
                   });
      duplicates.clear();
      duplicates = std::move(duplicatesFull);
  }

  return duplicates;
}
void printDuplicateMap(duplicate_map&& map) {
  std::function<std::string(hash)> hash2String = [](hash h) -> std::string {
    std::string result;
    result.reserve(32);

    for (std::size_t i = 0; i != 16; ++i) {
      result += "0123456789ABCDEF"[h[i] / 16];
      result += "0123456789ABCDEF"[h[i] % 16];
    }

    return result;
  };

  for (const std::pair<hash, std::set<fs::directory_entry>>& tuple : map) {
    std::cout << hash2String(tuple.first) << "\n";
    for (const fs::directory_entry& entry : tuple.second) {
      std::cout << "\t" << entry.path() << "\n";
    }
  }
  std::cout << "Duplicated files: " << map.size() << "\n";
}

int main(int argc, const char* argv[]) {
  // Default flags
  std::string path = "/home/mkarol/Dane/Test/";
  bool full = false;
  u_int threadNumber = 8;
  std::set<std::string> ignored;

  // Setting program options
  po::options_description description("Usage");

  description.add_options()
          ("help,h", "Displaying help message")
          ("version,v", "Get version")
          ("full,f", "Make full check")
          ("threads,t", po::value<u_int>()->default_value(1),"Number of worker threads")
          ("ignore,i", po::value<std::string>(),"Ignore extensions eg. \".exe;.class\"")
          ("path,p",po::value<std::string>()->default_value("."),"Duplicate check path");


  po::variables_map variablesMap;
  po::store(po::command_line_parser(argc, argv).options(description).run(),
            variablesMap);
  po::notify(variablesMap);

  if (variablesMap.count("help")) {
    std::cout << description << "\n";
    return 0;
  }

  if (variablesMap.count("version")) {
    std::cout << "Duplicate check <michal.p.karol@gmail.com> 1.00"
              << "\n";
    return 0;
  }

  if (variablesMap.count("full")) {
    full = true;
  }

  if (variablesMap.count("threads")) {
    threadNumber = variablesMap["threads"].as<u_int>();
  }

  if (variablesMap.count("ignore")) {
      std::string ignoredString = variablesMap["ignore"].as<std::string>();

      size_t pos = 0;
      std::string delimiter = ";";

      std::experimental::string_view view(ignoredString);

      while ((pos = view.find(delimiter)) != std::experimental::string_view::npos) {
          ignored.insert(view.substr(0, pos).to_string());
          view.remove_prefix(pos + delimiter.length());
      }
      ignored.insert(view.to_string());
  }

  if (variablesMap.count("path")) {
    path = variablesMap["path"].as<std::string>();
  }

  printDuplicateMap(findDuplicates(path, full, threadNumber, ignored));

  return 0;
}
