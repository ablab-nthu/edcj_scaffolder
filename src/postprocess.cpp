#include "utils.h"
#include <utility>
using std::pair;
using std::make_pair;

static void readGenome(string refFile, string tarFile, auto& ref, auto& tar, auto& tarOrder) {
	string contig;
	int id, family, tmp;
	ifstream fin(refFile);
	while (fin >> id >> family >> contig >> tmp) {
		ref.push_back(Marker(id, family, contig));
	}	fin.close();
	fin.open(tarFile);
	while (fin >> id >> family >> contig >> tmp) {
		tar.push_back(Marker(id, family, contig));
	}	fin.close();

	// tar contig order
	for (int i = 0; i < tar.size()-1; i++) {
		if (i == 0)
			tarOrder.push_back(tar[i].contig);
		if (tar[i].contig != tar[i+1].contig)
			tarOrder.push_back(tar[i+1].contig);
	}
}
static void readJoins(string filename, auto& tar, auto& tarTelos) {
	// construct tarTelos
	for (int i = 0; i < tar.size(); i++) {
		if (i == 0 || tar[i-1].contig != tar[i].contig)
			tarTelos[tar[i].contig].lhs = -1 * sign(tar[i].family) * tar[i].id;
		if (i == tar.size()-1 || tar[i].contig != tar[i+1].contig)
			tarTelos[tar[i].contig].rhs =      sign(tar[i].family) * tar[i].id;
	}
	// read joins.txt
	int t1, t2;
	string contig;
	ifstream fin(filename);
    while (fin >> contig >> t1 >> t2) {
		Telos& tp1 = tarTelos[tar[idx(t1)].contig];
		Telos& tp2 = tarTelos[tar[idx(t2)].contig];
		(tp1.lhs == t1 ? tp1.ljs : tp1.rjs) = t2 ;
		(tp2.lhs == t2 ? tp2.ljs : tp2.rjs) = t1 ;
	}	fin.close();
	for (auto& p: tarTelos)
		logging("{}, l: ({}, {}), r: ({}, {})\n", p.first,
			p.second.lhs, p.second.ljs, p.second.rhs, p.second.rjs);
}
static void readMergeContigs(string filename, auto& tar, auto& mergeContig) {
	int tmp;
	string key, contig;
	ifstream fin(filename);
    while (fin >> key) {
		vector<pair<string, int>> seg;
		while (fin >> contig) {
			if (contig == "end")
				break;
			fin >> tmp;
			seg.push_back(make_pair(contig, tmp));
		}	mergeContig[key] = seg;
	}	fin.close();

	for (auto& p: mergeContig) {
		logging("{}:\n", p.first);
		for (auto& cp: p.second)
			logging("  {} {}\n", cp.first, cp.second);
	}

	// 2023_0317: merge with joins
	/*for (auto& p: mergeContig) {
		fmt::print("joins {}:\n", p.first);
		for (int i = 0; i < p.second.size()-1; i++) {
			auto& tp1 = tarTelos[p.second[i  ].first];
			auto& tp2 = tarTelos[p.second[i+1].first];
			int t1 = p.second[i  ].second == 0 ? tp1.rhs: tp1.lhs;
			int t2 = p.second[i+1].second == 0 ? tp2.lhs: tp2.rhs;
			fmt::print("  {}, {}\n", t1, t2);
			joins[t1] = t2, joins[t2] = t1;
		}
	}*/
}
static void scaffolding(auto& tar, auto& tarTelos, auto& tarOrder, auto& scaffolds) {
	map<string, bool> visited;
	for (Marker& m: tar)
		visited[m.contig] = false;

	for (string& str: tarOrder) {
		vector<pair<string, int>> curScaffold;
		if (visited[str])
			continue;

		curScaffold.push_back(make_pair(str, 0));
		visited[str] = true;

		// traverse scaffolds
		Telos tp = tarTelos[str];
		int hs = tp.rhs, js = tp.rjs;
		while (js != 0) {
			string nc = tar[idx(js)].contig;
			if (visited[nc])
				break;

			Telos ntp = tarTelos[tar[idx(js)].contig];
			curScaffold.push_back(make_pair(nc, js == ntp.lhs ? 0 : 1));
			visited[nc] = true;

			hs = js == ntp.lhs ? ntp.rhs : ntp.lhs ;
			js = js == ntp.lhs ? ntp.rjs : ntp.ljs ;
		}	scaffolds.push_back(curScaffold);
	}
}
static void addReducedContigs(string filename, auto& scaffolds) {
	string contig;
	int id, family, tmp;
	ifstream fin(filename);
    while (fin >> contig) {
		scaffolds.push_back(vector<pair<string, int>>(1, make_pair(contig, 0)));
	}	fin.close();
}
static void outputScaffold(string filename, auto& scaffolds, auto& mergeContig) {
	auto mergeContigSign = [&](string target, auto merged) {
		// find target(leader) contig in the merged contigs
		// and then return its sign
		for (auto& mp: merged)
			if (mp.first == target)
				return mp.second;
		return -1;
	};

	ofstream fout(filename);
	for (int i = 0; i < scaffolds.size(); i++) {
		fout << format("> Scaffold_{}\n", i+1);
		for (auto& sp: scaffolds[i]) {
			auto it = mergeContig.find(sp.first);
			if (it != mergeContig.end()) {
				int sign = mergeContigSign(sp.first, it->second);
				logging("{}: {}, {}\n", sp.first, sp.second, sign);
				// leader contig sign XOR its sign in the merged contigs
				if (sp.second + sign == 1) {
					for (int i = it->second.size()-1; i > -1; i--)
						fout << format("{} {}\n", it->second[i].first, 1-it->second[i].second);
				} else {
					for (auto& mp: it->second)
						fout << format("{} {}\n", mp.first, mp.second);
				}
			} else
				fout << format("{} {}\n", sp.first, sp.second);
		}	fout << "\n";
	}	fout.close();
}
int postprocess(string refPath, string tarPath, string outDir) {
	logFile.open(outDir+"/postprocess.log");

	vector<Marker> ref, tar;
	vector<string> tarOrder;
	vector<vector<pair<string, int>>> scaffolds;

	map<string, Telos> tarTelos;
	map<string, vector<pair<string, int>>> mergeContig;

	// read files
	readGenome(refPath, tarPath, ref, tar, tarOrder);
	readJoins(outDir+"/joins.txt", tar, tarTelos);
	readMergeContigs(outDir+"/tar_merge.txt", tar, mergeContig);

	// scaffolding
	scaffolding(tar, tarTelos, tarOrder, scaffolds);
	addReducedContigs(outDir+"/removed_spd1.txt", scaffolds);

	// output final scaffold
	outputScaffold(outDir+"/scaffolds.txt", scaffolds, mergeContig);

	logFile.close();
	return 0;
}
