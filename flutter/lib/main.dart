import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  runApp(const InetRadioApp());
}

class InetRadioApp extends StatelessWidget {
  const InetRadioApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'inetRadio Controller',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.green),
        useMaterial3: true,
      ),
      home: const RadioHomePage(),
    );
  }
}

class RadioHomePage extends StatefulWidget {
  const RadioHomePage({super.key});

  @override
  State<RadioHomePage> createState() => _RadioHomePageState();
}

class _RadioHomePageState extends State<RadioHomePage> {
  List stations = [];
  int? currentStation;
  bool isPlaying = false;
  bool loading = false;
  String? errorMsg;
  String baseUrl = 'http://192.168.4.90'; // Default, can be changed by user
  final TextEditingController _ipController = TextEditingController();

  Color get stormBg => const Color(0xFF1b1f22);
  Color get cardBg => const Color(0xFF2a2f33);
  Color get accentGreen => const Color(0xFF4CAF50);
  Color get softGreen => const Color(0xFF8fc1a9);
  Color get stormyBtn => const Color(0xFF5c7b6b);

  @override
  void initState() {
    super.initState();
    _loadSavedIp();
  }

  Future<void> _loadSavedIp() async {
    final prefs = await SharedPreferences.getInstance();
    final savedIp = prefs.getString('esp32_ip') ?? '192.168.4.1';
    _ipController.text = savedIp;
    setState(() {
      baseUrl = 'http://$savedIp';
    });
    await _tryConnect();
  }

  Future<void> updateBaseUrl() async {
    final ip = _ipController.text.trim();
    setState(() {
      baseUrl = 'http://$ip';
    });
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('esp32_ip', ip);
    await _tryConnect();
  }

  Future<void> _tryConnect() async {
    setState(() {
      errorMsg = null;
    });
    try {
      await fetchStations();
      await fetchCurrentStation();
      if (stations.isEmpty) {
        setState(() {
          errorMsg = 'No stations found or connection failed.';
        });
      }
    } catch (e) {
      setState(() {
        errorMsg = 'Failed to connect: $e';
      });
    }
  }

  Future<void> fetchStations() async {
    setState(() => loading = true);
    try {
      final res = await http.get(Uri.parse('$baseUrl/stations'));
      if (res.statusCode == 200) {
        setState(() {
          stations = json.decode(res.body);
        });
      } else {
        setState(() {
          errorMsg = 'Failed to fetch stations.';
        });
      }
    } catch (e) {
      setState(() {
        errorMsg = 'Failed to fetch stations: $e';
      });
    }
    setState(() => loading = false);
  }

  Future<void> fetchCurrentStation() async {
    try {
      final res = await http.get(Uri.parse('$baseUrl/current-station'));
      if (res.statusCode == 200) {
        final data = json.decode(res.body);
        setState(() {
          currentStation = data['index'] as int?;
          isPlaying = (data['playing'] as bool?) ?? isPlaying;
        });
      }
    } catch (_) {}
  }

  Future<void> setStation(int index) async {
    await http.post(Uri.parse('$baseUrl/set-station'),
        headers: {'Content-Type': 'application/json'},
        body: json.encode({'index': index}));
    fetchCurrentStation();
  }

  Future<void> playPause() async {
    final endpoint = isPlaying ? '/pause' : '/start';
    await http.get(Uri.parse('$baseUrl$endpoint'));
    setState(() => isPlaying = !isPlaying);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: stormBg,
      appBar: AppBar(
        title: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.flash_on, color: accentGreen),
            const SizedBox(width: 8),
            const Text('Storm Radio'),
          ],
        ),
        backgroundColor: cardBg,
        foregroundColor: Colors.white,
        elevation: 0,
        toolbarHeight: 80,
        flexibleSpace: SafeArea(
          child: Center(
            child: Padding(
              padding: const EdgeInsets.only(top: 8.0),
              child: SizedBox(
                height: 56,
                child: Image.asset(
                  'assets/logo.svg',
                  package: null,
                  errorBuilder: (context, error, stackTrace) =>
                      const SizedBox(),
                ),
              ),
            ),
          ),
        ),
      ),
      body: Center(
        child: Container(
          constraints: const BoxConstraints(maxWidth: 420),
          margin: const EdgeInsets.symmetric(vertical: 32, horizontal: 8),
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: cardBg,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: const Color(0xFF3c474d)),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withOpacity(0.3),
                blurRadius: 10,
                offset: const Offset(3, 3),
              ),
            ],
          ),
          child: Column(
            mainAxisSize: MainAxisSize.max,
            children: [
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      controller: _ipController,
                      style: TextStyle(
                          color: softGreen,
                          fontWeight: FontWeight.bold,
                          fontSize: 16),
                      decoration: InputDecoration(
                        labelText: 'ESP32 IP Address',
                        labelStyle: TextStyle(color: softGreen),
                        border: const OutlineInputBorder(),
                        filled: true,
                        fillColor: cardBg,
                      ),
                      onSubmitted: (_) => updateBaseUrl(),
                    ),
                  ),
                ],
              ),
              if (errorMsg != null)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 8.0),
                  child: Text(
                    errorMsg!,
                    style: const TextStyle(
                        color: Colors.red, fontWeight: FontWeight.bold),
                  ),
                ),
              const SizedBox(height: 16),
              if (loading)
                const Center(child: CircularProgressIndicator())
              else ...[
                if (currentStation != null && stations.isNotEmpty)
                  Container(
                    margin: const EdgeInsets.only(bottom: 12),
                    padding: const EdgeInsets.symmetric(
                        vertical: 10, horizontal: 16),
                    decoration: BoxDecoration(
                      color: softGreen.withOpacity(0.2),
                      border: Border.all(color: softGreen),
                      borderRadius: BorderRadius.circular(10),
                    ),
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        const Icon(Icons.music_note, color: Color(0xFF8fc1a9)),
                        const SizedBox(width: 8),
                        Text(
                          'Now Playing: ',
                          style: TextStyle(
                              color: softGreen, fontWeight: FontWeight.bold),
                        ),
                        Text(
                          stations.firstWhere(
                              (s) => s['index'] == currentStation,
                              orElse: () => {'name': 'Unknown'})['name'],
                          style: const TextStyle(
                              color: Colors.white, fontWeight: FontWeight.bold),
                        ),
                      ],
                    ),
                  ),
                if (stations.isNotEmpty)
                  Flexible(
                    child: Padding(
                      padding: const EdgeInsets.symmetric(vertical: 8.0),
                      child: GridView.builder(
                        shrinkWrap: true,
                        physics: const AlwaysScrollableScrollPhysics(),
                        gridDelegate:
                            const SliverGridDelegateWithFixedCrossAxisCount(
                          crossAxisCount: 3,
                          mainAxisSpacing: 8,
                          crossAxisSpacing: 8,
                          childAspectRatio: 2.2,
                        ),
                        itemCount: stations.length,
                        itemBuilder: (context, i) {
                          final s = stations[i];
                          final isSelected = s['index'] == currentStation;
                          return ElevatedButton(
                            onPressed: () => setStation(s['index']),
                            style: ElevatedButton.styleFrom(
                              backgroundColor:
                                  isSelected ? accentGreen : cardBg,
                              foregroundColor:
                                  isSelected ? Colors.white : softGreen,
                              side: isSelected
                                  ? BorderSide(color: softGreen, width: 2)
                                  : BorderSide(color: cardBg),
                              shape: RoundedRectangleBorder(
                                  borderRadius: BorderRadius.circular(10)),
                              padding: const EdgeInsets.symmetric(vertical: 0),
                              elevation: isSelected ? 4 : 1,
                            ),
                            child: Row(
                              mainAxisAlignment: MainAxisAlignment.center,
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                if (isSelected)
                                  const Icon(Icons.radio,
                                      color: Colors.white, size: 18),
                                if (isSelected) const SizedBox(width: 4),
                                Flexible(
                                  child: Text(
                                    s['name'],
                                    overflow: TextOverflow.ellipsis,
                                    style: TextStyle(
                                      fontWeight: FontWeight.bold,
                                      color:
                                          isSelected ? Colors.white : softGreen,
                                      fontSize: 14,
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          );
                        },
                      ),
                    ),
                  ),
                Row(
                  children: [
                    Expanded(
                      child: ElevatedButton.icon(
                        icon: Icon(isPlaying ? Icons.pause : Icons.play_arrow,
                            color: Colors.white),
                        label: Text(isPlaying ? 'Pause' : 'Play',
                            style:
                                const TextStyle(fontWeight: FontWeight.bold)),
                        onPressed: playPause,
                        style: ElevatedButton.styleFrom(
                          backgroundColor: stormyBtn,
                          foregroundColor: Colors.white,
                          minimumSize: const Size.fromHeight(60),
                          shape: RoundedRectangleBorder(
                              borderRadius: BorderRadius.circular(12)),
                          textStyle: const TextStyle(fontSize: 18),
                          elevation: 2,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 20),
                Row(
                  children: [
                    Expanded(
                      child: ElevatedButton(
                        onPressed: fetchStations,
                        style: ElevatedButton.styleFrom(
                          backgroundColor: accentGreen,
                          foregroundColor: Colors.white,
                          shape: RoundedRectangleBorder(
                              borderRadius: BorderRadius.circular(12)),
                          padding: const EdgeInsets.symmetric(vertical: 16),
                        ),
                        child: const Text('Refresh Stations',
                            style: TextStyle(fontWeight: FontWeight.bold)),
                      ),
                    ),
                  ],
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}
