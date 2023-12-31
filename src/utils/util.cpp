#include "util.h"
#include <plugin-support.h>
#include <obs.h>
#include <vector>
#include <iostream>
#include "rapidjson/document.h"
#include "source_data.h"
#include <fstream>
#include <sstream>
#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

using namespace rapidjson;

std::string to_timestamp(int64_t t)
{
	int64_t sec = t / 1000;
	int64_t msec = t - sec * 1000;
	int64_t min = sec / 60;
	sec = sec - min * 60;

	char buf[32];
	snprintf(buf, sizeof(buf), "%02d:%02d.%03d", (int)min, (int)sec,
		 (int)msec);

	return std::string(buf);
}

void high_pass_filter(float *pcmf32, size_t pcm32f_size, float cutoff,
		      uint32_t sample_rate)
{
	const float rc = 1.0f / (2.0f * (float)M_PI * cutoff);
	const float dt = 1.0f / (float)sample_rate;
	const float alpha = dt / (rc + dt);
	float y = pcmf32[0];
	for (size_t i = 1; i < pcm32f_size; i++) {
		y = alpha * (y + pcmf32[i] - pcmf32[i - 1]);
		pcmf32[i] = y;
	}
}

// VAD (voice activity detection), return true if speech detected
bool vad_simple(float *pcmf32, size_t pcm32f_size, uint32_t sample_rate,
		float vad_thold, float freq_thold, bool verbose)
{
	const uint64_t n_samples = pcm32f_size;
	if (freq_thold > 0.0f) {
		high_pass_filter(pcmf32, pcm32f_size, freq_thold, sample_rate);
	}
	float energy_all = 0.0f;
	for (uint64_t i = 0; i < n_samples; i++) {
		energy_all += fabsf(pcmf32[i]);
	}
	energy_all /= (float)n_samples;
	if (verbose) {
		obs_log(LOG_INFO,
			"%s: energy_all: %f, vad_thold: %f, freq_thold: %f",
			__func__, energy_all, vad_thold, freq_thold);
	}
	if (energy_all < vad_thold) {
		return false;
	}
	return true;
}

void uploadcount(struct wyw_source_data *wf)
{
	std::string current_path =
		obs_frontend_get_current_record_output_path();
	std::string public_path = "/public_page.txt";
	std::string private_path = "/private_page.txt";

	std::fstream writeable;
	std::string fname = current_path + private_path;
	writeable.open(fname, std::ios::out);
	for (SizeType i = 0; i < wf->banlist.size(); i++) {
		writeable << wf->banlist[i] << " : "
			  << (wf->normalcnt ? ((float) wf->bancnt[i] /(float)wf->normalcnt * 100) : 0.f)
			<< "%" << std::endl;
	}

	writeable << "token :" << wf->normalcnt << std::endl;
	writeable.close();

	fname = current_path + public_path;
	writeable.open(fname, std::ios::out);
	int totalcnt = 0;
	for (SizeType i = 0; i < wf->banlist.size(); i++)
		totalcnt += wf->bancnt[i];

	writeable << "비속어 :" << (float)totalcnt / (float)(wf->normalcnt) * 100
		<< "%" << std::endl;
	writeable << "token :" << wf->normalcnt << std::endl;
	writeable.close();
}

bool getjson(void *data, const char *jsonStr)
{
	struct wyw_source_data *wf =
		static_cast<struct wyw_source_data *>(data);
	Document doc;
	wf->banlist.clear();
	wf->bantext.clear();
	wf->bancnt.clear();
	doc.Parse(jsonStr);
	if (doc.HasParseError()) {
		return false;
	}
	const Value &banArray = doc["ban"];
	for (SizeType i = 0; i < banArray.Size(); i++) {
		const Value &banObject = banArray[i];
		const char *word = banObject["word"].GetString();
		const Value &listArray = banObject["list"];
		wf->banlist.push_back(std::string(word));
		wf->bancnt.push_back(0);
		std::vector<std::string> tempList;
		for (SizeType j = 0; j < listArray.Size(); j++) {
			const char *listItem = listArray[j].GetString();
			tempList.push_back(std::string(listItem));
		}
		wf->bantext.push_back(tempList);
	}
	return true;
}
/* void getjson(void *data, char *jsonstring)
{

	struct wyw_source_data *wf = (struct wyw_source_data *)data;
	std::string json = (std::string)jsonstring;

	Document doc;
	doc.Parse(json.c_str());
	int i, j;
	std::vector<std::string> temp;
	Value &word = doc["ban"];
	for (i = 0; i < word.Size(); i++) {
		wf->banlist.push_back(word[i].GetString());
		Value &list = doc["ban"][i][1];
		for (j = 0; j < list.Size(); j++) {
			temp.push_back(list.GetString());
		}
		wf->bantext.push_back(temp);
	}
} */

void mkfile(std::string fname)
{
	std::fstream base(fname, std::ios::out);
	base << "\{\"key\":\"stat\",\"stat\" : [ \"2000-01-01 01:01\"]\}\n";
	base.close();
	return;
}

//example
//{
//	"key" : "stat"
//	"stat" : [
//	"2000-01-01",
//	"씨발","21%"
//	]
//}
//

/* void daystate(std::string date)
{
	std::vector<std::string> banname;
	std::vector<float> freq;
	std::ifstream readable;
	char *path = obs_frontend_get_current_record_output_path();
	std::string fname = *path + "result.txt";
	std::string json;
	readable.open(fname);
	if (!readable.is_open()) {
		//fileopen err
		return;
	}

	readable.seekg(0, std::ios::end);
	int size = readable.tellg();
	json.resize(size);
	readable.seekg(0, std::ios::beg);
	readable.read(&json[0], size);

	Document doc;
	doc.Parse(json.c_str());

	fstream write;
	write.open(date, ios::out);
	const Value &stat = doc["stat"];
	for (SizeType i = 0; i < stat.Size(); i++) {
		const Value &point = stat[i];
		if (date == point.GetString()) {
			write.write(date.c_str(), date.size());
			i++;
			while (strcmp(stat[i].GetString(), "end") == 0) {
				banname.push_back(stat[++i].GetString());
				std::string data = stat[++i].GetString();
				auto percentSymbolIterator = std::find(
					data.begin(), data.end(), '%');
				if (percentSymbolIterator != data.end()) {
					data.erase(percentSymbolIterator);
				}
				float result = std::stod(data);
				freq.push_back(result);
			}
		}
	}


	return;
}*/

void daystat(const std::string &root, const std::string &date)
{
	int token = 1;
	std::ifstream ifs(root);
	if (!ifs.is_open()) {
		return;
	}

	std::string jsonContent((std::istreambuf_iterator<char>(ifs)),
				(std::istreambuf_iterator<char>()));

	ifs.close();
	Document document;
	document.Parse(jsonContent.c_str());
	if (document.HasParseError()) {
		return;
	}

	const char *targetDate = date.c_str();
	bool foundDate = false;
	std::string broadcasttype;

	std::string current_path =
		obs_frontend_get_current_record_output_path();
	std::string result_path = "/data.txt";
	std::string fname = current_path + result_path;

	std::vector<std::string> banname;
	std::vector<int> bancnt;

	for (SizeType i = 0; i < document["stat"].Size(); ++i) {
		if (document["stat"][i].IsString() &&
		    strcmp(document["stat"][i].GetString(), targetDate) == 0) {
			foundDate = true;
			broadcasttype = document["stat"][i].GetString();
			i++;
			continue;
		}

		if (foundDate) {
			// 해당 날짜 이후부터 값을 파싱하여 파일에 저장합니다.

			std::string temp = document["stat"][i].GetString();
			if (!atoi(temp.c_str())) {
				banname.push_back(
					document["stat"][i].GetString());
				temp = document["stat"][i + 1].GetString();
				bancnt.push_back(atoi(temp.c_str()));
				i++;
			} else {
				token = atoi(temp.c_str());
				break;
			}
		}
	}

	std::ofstream ofs(fname, std::ofstream::out);
	ofs << targetDate << std::endl;
	ofs << broadcasttype << std::endl;
	for (int i = 0; i < banname.size(); i++) {
		ofs << banname[i] << " : "
		    << (float)bancnt[i] / (float)token * 100 << "%"
		    << std::endl;
	}
	ofs << "토큰 수 :" << token << std::endl;

	ofs.close();
}

void monstat(const std::string &root, const std::string &targetMonth)
{
	std::vector<std::string> banname;
	std::vector<int> bancnt;
	int token = 0;

	std::ifstream ifs(root);
	if (!ifs.is_open()) {
		return;
	}

	std::string current_path =
		obs_frontend_get_current_record_output_path();
	std::string result_path = "/data_month.txt";
	std::string fname = current_path + result_path;

	std::string jsonContent((std::istreambuf_iterator<char>(ifs)),
				(std::istreambuf_iterator<char>()));

	Document document;
	document.Parse(jsonContent.c_str());

	for (SizeType i = 0; i < document["stat"].Size(); i++) {
		std::string date = document["stat"][i].GetString();
		std::string sub = date.substr(0, 7);

		if (sub.compare(targetMonth)) {
			auto index = find(banname.begin(), banname.end(),
					  document["stat"][i].GetString());
			if (index == banname.end()) {
				std::string temp =
					document["stat"][i].GetString();
				if (!atoi(temp.c_str())) {
					banname.push_back(
						document["stat"][i].GetString());
					temp = document["stat"][i + 1]
						       .GetString();
					bancnt.push_back(atoi(temp.c_str()));
					i++;
				} else {
					token += atoi(temp.c_str());
				}
			} else {
				bancnt[index - banname.begin()] += atoi(
					document["stat"][i + 1].GetString());
			}
		}
	}

	// 파일에 월별 통계를 저장합니다.
	std::ofstream ofs(fname, std::ios::out);

	ofs << targetMonth << std::endl;
	for (SizeType i = 0; i < banname.size(); i++) {
		ofs << banname[i] << " : "
		    << (float)bancnt[i] / (float)token * 100 << "%"
		    << std::endl;
	}
	ofs << "토큰 수 :" << token << std::endl;
	ofs.close();
}

void frequency_write(void *data)
{
	struct wyw_source_data *wf = (struct wyw_source_data *)data;
	std::vector<std::string> bnd = wf->banlist;
	std::vector<std::int16_t> cnt = wf->bancnt;
	std::vector<float> ps;
	if (wf->normalcnt == 0) {
		return;
	}

	for (int i = 0; i < cnt.size(); i++) {
		float a = (float)cnt[i] / (float)(wf->normalcnt);
		ps.push_back(a);
	}
	int i = 0;
	time_t cTime = time(NULL);
	struct tm *pLocal = localtime(&cTime);
	std::fstream writeable;
	std::string ftext;
	std::string current_path =
		obs_frontend_get_current_record_output_path();
	std::string result_path = "/result.txt";
	std::string fname = current_path + result_path;
	obs_log(LOG_INFO, "Recording stopped. write %s.", fname.c_str());
	writeable.open(fname, std::ios::in);
	if (!writeable.is_open()) {
		mkfile(fname);
	} else {
		writeable.close();
	}
	std::string gline;

	writeable.open(fname, std::ios::in);
	while (getline(writeable, gline)) {
		ftext = ftext + gline;
	}
	if (ftext.length() > 3)
		ftext.erase(ftext.length() - 2, 2);
	writeable.close();

	writeable.open(fname, std::ios::out);
	if (!writeable.is_open()) {
		//fileopen err
		obs_log(LOG_INFO, "fopen err");
		return;
	}

	writeable.write(ftext.c_str(), ftext.size());
	obs_log(LOG_INFO, "ftext write");
	std::string tmp;
	char buf[50];
	sprintf(buf, ",\"%04d-%02d-%02d %02d:%02d\"", pLocal->tm_year + 1900,
		pLocal->tm_mon + 1, pLocal->tm_mday, pLocal->tm_hour,
		pLocal->tm_min);
	tmp = (std::string)buf;
	writeable.write(tmp.c_str(), tmp.size());

	if (wf->broadcast_type == nullptr) {
		writeable.write(",\"none\"", 7);
	} else {
		tmp = ",\"" + (std::string)wf->broadcast_type + "\"";
		writeable.write(tmp.c_str(), tmp.size());
	}

	while (i < bnd.size()) {
		tmp = ",\"";
		tmp.append(bnd[i]).append("\"");
		sprintf(buf, ",\"%d\"", cnt[i]);
		std::string pers = (std::string)buf;
		writeable.write(tmp.c_str(), tmp.size());
		writeable.write(pers.c_str(), pers.size());
		i++;
	}
	{
		sprintf(buf, ",\"%d\"", wf->normalcnt);
		std::string pers = (std::string)buf;
		writeable.write(pers.c_str(), pers.size());
	}

	writeable.write("]}", 2);
	writeable.close();
}

char *readJsonFromFile(const char *filePath)
{
	std::ifstream file(filePath);
	if (!file.is_open()) {
		std::cerr << "Error opening file: " << filePath << std::endl;
		return nullptr; // Changed to return nullptr in case of error
	}
	std::string jsonString((std::istreambuf_iterator<char>(file)),
			       std::istreambuf_iterator<char>());
	file.close();
	char *result = new char[jsonString.length() + 1];
	strcpy(result, jsonString.c_str());
	return result;
}