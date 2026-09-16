const ROOT_URL = window.location.href       // for production code
// const ROOT_URL = "http://localhost:3000/"   // for testing with local json server
let conn_status = 0;		// connection status to the ESP32
let conn_status_initialized = false;
let stats_request_in_flight = false;
let static_refresh_in_flight = null;
let static_data_ready = false;
let initial_static_retry_used = false;
let serial_via_JTAG = 0;	// set to 1 if ESP32 is using the USB interface as serial interface for data and not using the UART. If 0 we set UART config to invisible for the user.
let last_byte_count = 0;
let last_timestamp_byte_count = 0;
let esp_chip_model = 0;		// according to get_esp_chip_model_str()
let recv_ser_bytes = 0;		// Total bytes received from serial interface
let serial_dec_mav_msgs = 0;	// Total MAVLink messages decoded from serial interface
let set_telem_proto = null;		// Telemetry protocol received by the ESP32

const CHANNELS_2G = [
	{ val: "1", text: "1 (2412 MHz)" },
	{ val: "2", text: "2 (2417 MHz)" },
	{ val: "3", text: "3 (2422 MHz)" },
	{ val: "4", text: "4 (2427 MHz)" },
	{ val: "5", text: "5 (2432 MHz)" },
	{ val: "6", text: "6 (2437 MHz)" },
	{ val: "7", text: "7 (2442 MHz)" },
	{ val: "8", text: "8 (2447 MHz)" },
	{ val: "9", text: "9 (2452 MHz)" },
	{ val: "10", text: "10 (2457 MHz)" },
	{ val: "11", text: "11 (2462 MHz)" },
	{ val: "12", text: "12 (2467 MHz)" },
	{ val: "13", text: "13 (2472 MHz)" },
];

const CHANNELS_5G = [
	{ val: "36", text: "36 (5180 MHz)" },
	{ val: "40", text: "40 (5200 MHz)" },
	{ val: "44", text: "44 (5220 MHz)" },
	{ val: "48", text: "48 (5240 MHz)" },
	{ val: "149", text: "149 (5745 MHz)" },
	{ val: "153", text: "153 (5765 MHz)" },
	{ val: "157", text: "157 (5785 MHz)" },
	{ val: "161", text: "161 (5805 MHz)" },
	{ val: "165", text: "165 (5825 MHz)" },
];

/**
 * Populates the Wi-Fi channel dropdown based on the selected band.
 * @param {string} band "2.4" or "5"
 * @param {string|number} selected_channel Channel number to select
 */
function populate_wifi_channels(band, selected_channel) {
	const chanSelect = document.getElementById("wifi_chan");
	if (!chanSelect) return;
	chanSelect.innerHTML = "";
	const list = (band === "5") ? CHANNELS_5G : CHANNELS_2G;
	let matched = false;
	const selStr = String(selected_channel);
	list.forEach(item => {
		const opt = document.createElement("option");
		opt.value = item.val;
		opt.textContent = item.text;
		if (item.val === selStr) {
			opt.selected = true;
			matched = true;
		}
		chanSelect.appendChild(opt);
	});
	if (!matched && list.length > 0) {
		chanSelect.value = (band === "5") ? "36" : "6";
	}
}

/**
 * Handles user changing the Wi-Fi band selector.
 */
function on_wifi_band_changed() {
	const bandSelect = document.getElementById("wifi_band_select");
	const band = bandSelect ? bandSelect.value : "2.4";
	const defaultChan = (band === "5") ? "36" : "6";
	populate_wifi_channels(band, defaultChan);
	update_wifi_band_notice(band, defaultChan);
}

/**
 * Handles user changing the channel within the current band.
 */
function on_wifi_channel_changed() {
	const bandSelect = document.getElementById("wifi_band_select");
	const band = bandSelect ? bandSelect.value : "2.4";
	const chanSelect = document.getElementById("wifi_chan");
	const chan = chanSelect ? chanSelect.value : (band === "5" ? "36" : "6");
	update_wifi_band_notice(band, chan);
}

/**
 * Updates the reminder notice banner when the band or channel is changed.
 * @param {string} band "2.4" or "5"
 * @param {string|number} channel Selected channel number
 */
function update_wifi_band_notice(band, channel) {
	const noticeDiv = document.getElementById("wifi_band_notice");
	const noticeText = document.getElementById("wifi_band_notice_text");
	if (!noticeDiv || !noticeText) return;
	if (band === "5") {
		noticeDiv.style.display = "block";
		noticeText.innerHTML = "<strong>Notice:</strong> 5 GHz Band selected (Channel " + channel + "). Click <strong>Save Settings & Reboot</strong> to apply.<br><em>Ensure your connecting device (phone/laptop) supports 5 GHz Wi-Fi to reconnect.</em>";
	} else {
		noticeDiv.style.display = "none";
	}
}

function change_radio_dis_arm_visibility() {
	// we only support this feature when MAVLink or LTM are set AND when a standard Wi-Fi mode or BLE is enabled
	let radio_dis_onarm_div = document.getElementById("radio_dis_onarm_div")
	if ((document.getElementById("esp32_mode").value > "2" &&  document.getElementById("esp32_mode").value < "6") || document.getElementById("proto").value === "5") {
		radio_dis_onarm_div.style.display = "none";
	} else {
		radio_dis_onarm_div.style.display = "block";
	}
}

function change_ap_ip_visibility() {
	const esp32Mode = document.getElementById("esp32_mode").value;
	const elements = {
		ap_ip_div: document.getElementById("ap_ip_div"),
		ap_band_div: document.getElementById("ap_band_div"),
		ap_channel_div: document.getElementById("ap_channel_div"),
		wifi_band_notice: document.getElementById("wifi_band_notice"),
		lr_disclaimer_div: document.getElementById("esp-lr-ap-disclaimer"),
		ble_disclaimer_div: document.getElementById("ble_disclaimer_div"),
		wifi_ssid_div: document.getElementById("wifi_ssid_div"),
		wifi_en_gn_div: document.getElementById("wifi_en_gn_div"),
		static_ip_config_div: document.getElementById("static_ip_config_div"),
		pass_div: document.getElementById("pass_div"),
		espnow_secret_div: document.getElementById("espnow_secret_div"),
		espnow_rotate_secret_button: document.getElementById("espnow_rotate_secret_button"),
	};

	if (esp32Mode === "2") {
		elements.ap_ip_div.style.display = "none";
		if (elements.ap_band_div) elements.ap_band_div.style.display = "none";
		elements.ap_channel_div.style.display = "none";
		if (elements.wifi_band_notice) elements.wifi_band_notice.style.display = "none";
		elements.wifi_en_gn_div.style.display = "block";
		elements.static_ip_config_div.style.display = "block";
	} else {
		elements.ap_ip_div.style.display = "block";
		if (elements.ap_band_div) elements.ap_band_div.style.display = "block";
		elements.ap_channel_div.style.display = "block";
		elements.wifi_en_gn_div.style.display = "none";
		elements.static_ip_config_div.style.display = "none";
	}

	if (esp32Mode === "6") {
		elements.ble_disclaimer_div.style.display = "block";
		elements.wifi_ssid_div.style.display = "none";
		if (elements.ap_band_div) elements.ap_band_div.style.display = "none";
		elements.ap_channel_div.style.display = "none";
		elements.pass_div.style.display = "none";
		elements.ap_ip_div.style.display = "none";
		if (elements.wifi_band_notice) elements.wifi_band_notice.style.display = "none";
	} else {
		elements.ble_disclaimer_div.style.display = "none";
		elements.wifi_ssid_div.style.display = "block";
		elements.pass_div.style.display = "block";
	}

	if (esp32Mode > "2" && esp32Mode < "6") {
		elements.lr_disclaimer_div.style.display = "block";
	} else {
		elements.lr_disclaimer_div.style.display = "none";
	}

	if (esp32Mode > "3" && esp32Mode < "6") {
		elements.ap_ip_div.style.display = "none";
		elements.wifi_ssid_div.style.visibility = "hidden";
		elements.espnow_secret_div.style.display = "block";
		elements.espnow_rotate_secret_button.style.display = esp32Mode === "5" ? "inline-block" : "none";
	} else {
		elements.wifi_ssid_div.style.visibility = "visible";
		elements.espnow_secret_div.style.display = "none";
		elements.espnow_rotate_secret_button.style.display = "none";
	}
	change_radio_dis_arm_visibility();
	change_espnow_air2air_visibility();
}

/**
 * Shows AIR-to-AIR MAVLink forwarding only where it can safely apply.
 */
function change_espnow_air2air_visibility() {
	const espnow_air2air_div = document.getElementById("espnow_air2air_div");
	const esp32_mode = document.getElementById("esp32_mode");
	const telemetry_protocol = document.getElementById("proto");
	espnow_air2air_div.style.display = esp32_mode.value === "4" && telemetry_protocol.value === "4" ? "block" : "none";
}

function change_msp_ltm_visibility(){
	let msp_ltm_div = document.getElementById("msp_ltm_div");
	let trans_pack_size_div = document.getElementById("trans_pack_size_div");
	let rep_rssi_dbm_div = document.getElementById("rep_rssi_dbm_div");
	let telem_proto = document.getElementById("proto");
	if (telem_proto.value === "1") {
		msp_ltm_div.style.display = "block";
		trans_pack_size_div.style.display = "none";

	} else {
		msp_ltm_div.style.display = "none";
		trans_pack_size_div.style.display = "block";
	}
	if (telem_proto.value === "4") {
		rep_rssi_dbm_div.style.display = "block";
	} else {
		rep_rssi_dbm_div.style.display = "none";
	}
	change_radio_dis_arm_visibility();
	change_espnow_air2air_visibility();
}

function change_uart_visibility() {
	let tx_rx_div = document.getElementById("tx_rx_div");
	let rts_cts_div = document.getElementById("rts_cts_div");
	let rts_thresh_div = document.getElementById("rts_thresh_div");
	let baud_div = document.getElementById("baud_div");
	if (serial_via_JTAG === 0) {
		rts_cts_div.style.display = "block";
		tx_rx_div.style.display = "block";
		rts_thresh_div.style.display = "block";
		baud_div.style.display = "block";
	} else {
		rts_cts_div.style.display = "none";
		tx_rx_div.style.display = "none";
		rts_thresh_div.style.display = "none";
		baud_div.style.display = "none";
	}
}

function flow_control_check() {
	let gpio_rts = document.getElementById("gpio_rts");
	let gpio_cts = document.getElementById("gpio_cts");
	if (isNaN(gpio_rts.value) || isNaN(gpio_cts.value) || gpio_cts.value === '' || gpio_rts.value === '' || gpio_rts.value === gpio_cts.value) {
		show_toast("UART flow control disabled.")
	} else {
		show_toast("UART flow control enabled. Make sure RTS & CTS pins are connected!");
	}
}

/**
 * Convert a form into a JSON string
 * @param form The HTML form to convert
 * @returns {string} JSON formatted string
 */
function toJSONString(form) {
	let obj = {}
	let elements = form.querySelectorAll("input, select")
	for (let i = 0; i < elements.length; ++i) {
		let element = elements[i]
		let name = element.name;
		let value = element.value;
		// parse numbers as numbers except for the SSID and the password fields
		if (!isNaN(Number(value)) && (name.localeCompare("wifi_ssid") !== 0) && (name.localeCompare("wifi_pass") !== 0)) {
			if (name) {
				obj[name] = parseInt(value)
			}
		} else {
			if (name) {
				if (element.type === "checkbox") {
					// convert checked/not checked to 1 & 0 as value
					obj[name] = element.checked ? 1 : 0;
				} else {
					// just get the value specified by the input/select
					obj[name] = value
				}
			}
		}
	}
	return JSON.stringify(obj)
}

/**
 * Request data from the ESP to display in the GUI
 * @param api_path API path/request path
 * @returns {Promise<any>}
 */
async function get_json(api_path) {
	let req_url = ROOT_URL + api_path;

	const controller = new AbortController()
	// Set a timeout limit for the request using `setTimeout`. If the body
	// of this timeout is reached before the request is completed, it will
	// be cancelled.

	const timeout = setTimeout(() => {
		controller.abort()
	}, 1000)
	try {
		const response = await fetch(req_url, {
			signal: controller.signal
		});
		if (!response.ok) {
			const message = `An error has occured: ${response.status}`;
			throw new Error(message);
		}
		return await response.json();
	} finally {
		clearTimeout(timeout);
	}
}

/**
 * Rotates the ESP-NOW group secret after warning that every AIR unit must be rebound.
 */
async function rotate_espnow_secret() {
	if (!confirm("Rotate the ESP-NOW link secret? All currently bound AIR units will disconnect and must be rebound.")) {
		return;
	}
	try {
		const response = await send_json("api/settings/espnow-secret/rotate");
		document.getElementById("espnow_secret").value = response.espnow_secret;
		show_toast(response.msg);
	} catch (error) {
		show_toast(error.message);
	}
}

/**
 * Create a response with JSON data attached
 * @param api_path API URL path
 * @param json_data JSON body data to send
 * @returns {Promise<any>}
 */
async function send_json(api_path, json_data = undefined) {
	let post_url = ROOT_URL + api_path;
	const response = await fetch(post_url, {
		method: 'POST',
		headers: {
			'Accept': 'application/json',
			'Content-Type': 'application/json',
			"charset": 'utf-8'
		},
		body: json_data
	});
	if (!response.ok) {
		const message = `An error has occured: ${response.status}`;
		throw new Error(message);
	}
	return await response.json();
}

function get_esp_chip_model_str(esp_model_index) {
	switch (esp_model_index) {
		default:
		case 0:
			return "unknown/unsupported ESP32 chip";
		case 1:
			return "ESP32";
		case 2:
			return "ESP32-S2";
		case 9:
			return "ESP32-S3";
		case 5:
			return "ESP32-C3";
		case 13:
			return "ESP32-C6";
		case 12:
			return "ESP32-C2";
		case 23:
			return "ESP32-C5";
	}
}

/**
 * Fetch system information and apply it to the user interface.
 * @returns {Promise<boolean>} True when the response was applied successfully
 */
async function get_system_info() {
	try {
		const json_data = await get_json("api/system/info");
		console.log("Received system info: " + json_data)
		document.getElementById("about").innerHTML = "DroneBridge for ESP32 v" + json_data["major_version"] +
			"." + json_data["minor_version"] + "." + json_data["patch_version"] + " ("+json_data["maturity_version"]+")" +
			" - esp-idf " + json_data["idf_version"] + " - " + get_esp_chip_model_str(json_data["esp_chip_model"])
		document.getElementById("esp_mac").innerHTML = json_data["esp_mac"]
		serial_via_JTAG = json_data["serial_via_JTAG"];
		// set external antenna option visible based on info if RF switch is available on the board
		if (parseInt(json_data["has_rf_switch"]) === 1) {
			document.getElementById("ant_use_ext_div").style.display = "block";
		} else {
			document.getElementById("ant_use_ext_div").style.display = "none";
		}
		esp_chip_model = json_data["esp_chip_model"];
		const has5g = (parseInt(json_data["has_5g_support"]) === 1) || (esp_chip_model === 23);
		const band5gOption = document.getElementById("wifi_band_5g_option");
		if (band5gOption) {
			if (has5g) {
				band5gOption.hidden = false;
				band5gOption.disabled = false;
			} else {
				band5gOption.hidden = true;
				band5gOption.disabled = true;
			}
		}
		return true;
	} catch (error) {
		console.error("Failed to load or display system information:", error);
		return false;
	}
}

/**
 * Retry the initial static data request once after stats confirms connectivity.
 */
function retry_initial_static_data_if_needed() {
	if (conn_status === 1 && !static_data_ready && static_refresh_in_flight === null && !initial_static_retry_used) {
		initial_static_retry_used = true;
		refresh_static_data();
	}
}

/**
 * Fetch system information and settings without allowing overlapping refreshes.
 * @returns {Promise<boolean>} True when both responses were applied successfully
 */
function refresh_static_data() {
	if (static_refresh_in_flight !== null) {
		return static_refresh_in_flight;
	}

	static_refresh_in_flight = Promise.all([get_system_info(), get_settings()])
		.then(results => {
			const refresh_succeeded = results.every(result => result === true);
			if (refresh_succeeded) {
				static_data_ready = true;
				check_for_issues();
			}
			return refresh_succeeded;
		})
		.finally(() => {
			static_refresh_in_flight = null;
			setTimeout(retry_initial_static_data_if_needed, 0);
		});

	return static_refresh_in_flight;
}

/**
 * Record connectivity reported by stats polling and refresh static data after reconnecting.
 * @param {boolean} is_connected True when the latest stats request succeeded
 */
function set_connection_status(is_connected) {
	const new_status = is_connected ? 1 : 0;
	const previous_status = conn_status;
	const was_initialized = conn_status_initialized;

	conn_status = new_status;
	conn_status_initialized = true;

	if (!was_initialized) {
		if (new_status === 1) {
			retry_initial_static_data_if_needed();
		}
		return;
	}

	if (previous_status === 0 && new_status === 1) {
		refresh_static_data();
	}
}

/**
 * Update the ESP32 connection indicator.
 */
function update_conn_status() {
	if (conn_status)
		document.getElementById("web_conn_status").innerHTML = "<span class=\"dot_green\"></span> connected to ESP32"
	else {
		document.getElementById("web_conn_status").innerHTML = "<span class=\"dot_red\"></span> disconnected from ESP32"
		document.getElementById("current_client_ip").innerHTML = ""
	}
}

/**
 * Get connection status information and display it in the GUI
 */
async function get_stats() {
	if (stats_request_in_flight) {
		return;
	}

	stats_request_in_flight = true;
	try {
		let json_data;
		try {
			json_data = await get_json("api/system/stats");
			set_connection_status(true);
		} catch (error) {
			set_connection_status(false);
			console.warn("Failed to fetch system statistics:", error);
			return;
		}

		try {
		let d = new Date();
		recv_ser_bytes = parseInt(json_data["read_bytes"]);
		serial_dec_mav_msgs = parseInt(json_data["serial_dec_mav_msgs"]);
		let bytes_per_second = 0;
		let current_time = d.getTime();
		if (last_byte_count > 0 && last_timestamp_byte_count > 0 && !isNaN(recv_ser_bytes)) {
			bytes_per_second = (recv_ser_bytes - last_byte_count) / ((current_time - last_timestamp_byte_count) / 1000);
		}
		last_timestamp_byte_count = current_time;
		if (!isNaN(recv_ser_bytes) && recv_ser_bytes > 1000000) {
			document.getElementById("read_bytes").innerHTML = (recv_ser_bytes / 1000000).toFixed(3) + " MB (" + ((bytes_per_second*8)/1000).toFixed(2) + " kbit/s)"
		} else if (!isNaN(recv_ser_bytes) && recv_ser_bytes > 1000) {
			document.getElementById("read_bytes").innerHTML = (recv_ser_bytes / 1000).toFixed(2) + " kB (" + ((bytes_per_second*8)/1000).toFixed(2) + " kbit/s)"
		} else if (!isNaN(recv_ser_bytes)) {
			document.getElementById("read_bytes").innerHTML = recv_ser_bytes + " bytes (" + Math.round(bytes_per_second) + " byte/s)"
		}
		last_byte_count = recv_ser_bytes;

		let tcp_clients = parseInt(json_data["tcp_connected"])
		if (!isNaN(tcp_clients) && tcp_clients === 1) {
			document.getElementById("tcp_connected").innerHTML = tcp_clients + " client"
		} else if (!isNaN(tcp_clients)) {
			document.getElementById("tcp_connected").innerHTML = tcp_clients + " clients"
		}
		// UDP clients for tooltip
		let udp_clients_string = ""
		if (json_data.hasOwnProperty("udp_clients")) {
			let udp_conn_jsonarray = json_data["udp_clients"];
			for (let i = 0; i < udp_conn_jsonarray.length; i++) {
				udp_clients_string = udp_clients_string + udp_conn_jsonarray[i];
				if ((i + 1) !== udp_conn_jsonarray.length) {
					udp_clients_string = udp_clients_string + "<br>";
				}
			}
			if (udp_conn_jsonarray.length === 0) {
				udp_clients_string = "-";
			} else {
				document.getElementById("tooltip_udp_clients").innerHTML = udp_clients_string;
			}
		}

		let udp_clients = parseInt(json_data["udp_connected"])
		if (!isNaN(udp_clients) && udp_clients === 1) {
			document.getElementById("udp_connected").innerHTML = "<span class=\"tooltiptext\" id=\"tooltip_udp_clients\">"+udp_clients_string+"</span>" + udp_clients + " client"
		} else if (!isNaN(udp_clients)) {
			document.getElementById("udp_connected").innerHTML = "<span class=\"tooltiptext\" id=\"tooltip_udp_clients\">"+udp_clients_string+"</span>" + udp_clients + " clients"
		}

		if ('esp_rssi' in json_data) {
			let rssi = parseInt(json_data["esp_rssi"])
			if (!isNaN(rssi) && rssi < 0) {
				document.getElementById("current_client_ip").innerHTML = "IP Address: " + json_data["current_client_ip"] + "<br />Signal Strength: " + rssi + "dBm"
			} else if (!isNaN(rssi)) {
				document.getElementById("current_client_ip").innerHTML = "IP Address: " + json_data["current_client_ip"]
			}
		} else if ('connected_sta' in json_data) {
			let a = ""
			json_data["connected_sta"].forEach((item) => {
				a = a + "Client: " + item.sta_mac + " Signal Strength: " + item.sta_rssi + "dBm<br />"
			});
			document.getElementById("current_client_ip").innerHTML = a
		}

		} catch (error) {
			console.error("Failed to display system statistics:", error);
		}
	} finally {
		stats_request_in_flight = false;
	}
}

/**
 * Get settings from ESP and display them in the GUI. JSON objects have to match the element ids
 * @returns {Promise<boolean>} True when the response was applied successfully
 */
async function get_settings() {
	try {
		const json_data = await get_json("api/settings");
		console.log("Received settings: " + json_data)
		for (const key in json_data) {
			if (json_data.hasOwnProperty(key)) {
				if (key === "wifi_chan") {
					let chanVal = parseInt(json_data[key]);
					let band = chanVal > 14 ? "5" : "2.4";
					let bandElem = document.getElementById("wifi_band_select");
					if (bandElem) bandElem.value = band;
					populate_wifi_channels(band, chanVal);
					continue;
				}
				let elem = document.getElementById(key)
				if (elem != null) {
					if (elem.type === "checkbox") {
						// translate 1 & 0 to checked and not checked
						elem.checked = json_data[key] === 1;
					} else {
						elem.value = json_data[key] + ""
					}
				}
			}
		}
		set_telem_proto = document.getElementById("proto").value;
		change_ap_ip_visibility();
		change_msp_ltm_visibility();
		check_for_issues();
		return true;
	} catch (error) {
		console.error("Failed to load or display settings:", error);
		show_toast(error.message);
		return false;
	}
}

function add_new_udp_client() {
	let ip = prompt("Please enter the IP address of the UDP receiver", "192.168.2.X");
	if (ip == null) {
		show_toast("Operation cancelled by user."); 
		return;
	}
	let port = prompt("Please enter the port number of the UDP receiver", "14550");
	if (port == null) {
		show_toast("Operation cancelled by user.")
		return;
	}
	port = parseInt(port);
	let save_to_nvm = confirm("Save this UDP client to the permanent storage so it will be auto added after reboot/reset?\nYou can only save one UDP client to the memory. The old ones will be overwritten.\nSelect no if you only want to add this client for this session.");
	const ippattern = /^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;

	if (ip != null && port != null && ippattern.test(ip)) {
		let myjson = {
			udp_client_ip: ip,
			udp_client_port: port,
			save: save_to_nvm
		};
		send_json("api/settings/clients/udp", JSON.stringify(myjson)).then(send_response => {
			console.log(send_response);
			show_toast(send_response["msg"])
		}).catch(error => {
			show_toast(error.message);
		});
	} else {
		show_toast("Error: Enter valid IP and port!")
	}
}

async function clear_udp_clients() {
	if (confirm("Do you want to remove all UDP connections?\nGCS will have to re-connect.") === true) {
		let post_url = ROOT_URL + "api/settings/clients/clear_udp";
		const response = await fetch(post_url, {
			method: 'DELETE',
			headers: {
				'Accept': 'application/json',
				'Content-Type': 'application/json',
				"charset": 'UTF-8'
			},
			body: null
		});
		if (!response.ok) {
			const message = `An error has occured: ${response.status}`;
			throw new Error(message);
		}
	} else {
		// cancel
	}
}

function show_toast(msg, background_color = "#0058a6") {
	Toastify({
		text: msg,
		duration: 5000,
		newWindow: true,
		close: true,
		gravity: "top", // `top` or `bottom`
		position: "center", // `left`, `center` or `right`
		style: {
			background: background_color,
			color: "#ff9734",
			borderColor: "#ff9734",
			borderStyle: "solid",
			borderRadius: "2px",
			borderWidth: "1px",
		},
		stopOnFocus: true, // Prevents dismissing of toast on hover
	}).showToast();
}

function check_validity() {
	let valid = true;
	let wifi_pass = document.getElementById("wifi_pass")
	if (!wifi_pass.checkValidity()) {
		show_toast("Error: 8<(password length)<64");
		valid = false;
	}
	let espnow_secret = document.getElementById("espnow_secret")
	if (!espnow_secret.checkValidity()) {
		show_toast("Error: ESP-NOW link secret must be 43 Base64URL characters");
		valid = false;
	}
	return valid;
}

/**
 * Update the frontend information boxes based on current serial and MAVLink state.
 */
function check_for_issues() {
	let issue_div = document.getElementById("issue_div");
	if (set_telem_proto === "4" && serial_dec_mav_msgs === 0 && recv_ser_bytes !== 0) {
		issue_div.style.display = "block";
	} else {
		issue_div.style.display = "none";
	}

	let uart_pins_info_div = document.getElementById("uart_pins_info_div");
	let gpio_tx = document.getElementById("gpio_tx");
	let gpio_rx = document.getElementById("gpio_rx");
	let uart_pins_not_configured = static_data_ready && serial_via_JTAG === 0 &&
		gpio_tx.value !== "" && gpio_rx.value !== "" && gpio_tx.value === gpio_rx.value;
	if (uart_pins_not_configured) {
		uart_pins_info_div.style.display = "block";
	} else {
		uart_pins_info_div.style.display = "none";
	}
}

/**
 * Displays the reboot countdown overlay modal after saving settings.
 * @param {boolean} is5g True if new settings configure 5 GHz Wi-Fi
 * @param {string|number} channel Channel number
 */
function show_reboot_status_modal(is5g, channel) {
	const modal = document.getElementById("reboot_modal");
	const title = document.getElementById("reboot_modal_title");
	const body = document.getElementById("reboot_modal_body");
	const timer = document.getElementById("reboot_modal_timer");
	if (!modal || !body || !timer) return;

	modal.style.display = "block";
	title.textContent = "Rebooting ESP32...";

	if (is5g) {
		body.innerHTML = "<b>Configured Mode:</b> 5 GHz Wi-Fi (Channel " + channel + ")<br><br>" +
			"• Your connection to the ESP32 will disconnect momentarily.<br>" +
			"• The ESP32 is rebooting and launching the Access Point on 5 GHz.<br>" +
			"• Please wait ~5 seconds, then open your device Wi-Fi settings and reconnect to <b>DroneBridge ESP32</b>.";
	} else {
		body.innerHTML = "<b>Configured Mode:</b> 2.4 GHz Wi-Fi (Channel " + channel + ")<br><br>" +
			"• Your connection to the ESP32 will disconnect momentarily.<br>" +
			"• The ESP32 is rebooting to apply the new settings.<br>" +
			"• Please reconnect your device to <b>DroneBridge ESP32</b> once it restarts.";
	}

	let countdown = 8;
	timer.textContent = "Reconnecting in " + countdown + " seconds...";
	const interval = setInterval(() => {
		countdown--;
		if (countdown > 0) {
			timer.textContent = "Reconnecting in " + countdown + " seconds...";
		} else {
			clearInterval(interval);
			timer.textContent = "Reloading page...";
			setTimeout(() => {
				window.location.reload();
			}, 1000);
		}
	}, 1000);
}

/**
 * Validates, serializes, and saves the settings form via REST API,
 * prompts the user with confirmation, and displays a reboot countdown modal.
 */
function save_settings() {
	let form = document.getElementById("settings_form");
	if (check_validity()) {
		let chan = parseInt(document.getElementById("wifi_chan").value);
		let is5g = chan > 14;
		let confirmMsg = is5g
			? "You have selected 5 GHz mode on Channel " + chan + ".\n\nAfter rebooting, the ESP32 will broadcast its Access Point on 5 GHz. Your phone or laptop must support 5 GHz Wi-Fi to reconnect.\n\nSave settings and reboot now?"
			: "Save all settings and reboot the ESP32 now?";
		if (!confirm(confirmMsg)) {
			return;
		}
		let json_data = toJSONString(form);
		show_toast("Saving settings & rebooting ESP32...", "#0058a6");
		send_json("api/settings", json_data).then(send_response => {
			console.log(send_response);
			show_toast(send_response["msg"] || "Settings saved! Rebooting...", "#28a745");
			show_reboot_status_modal(is5g, chan);
		}).catch(error => {
			show_toast(error.message, "#dc3545");
		});
	} else {
		console.log("Form was not filled out correctly.");
	}
}
