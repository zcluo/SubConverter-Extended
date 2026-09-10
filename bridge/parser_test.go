package main

import (
	"encoding/base64"
	"strings"
	"testing"
)

const realityMlkemTestBase = "vless://22222222-2222-4222-8222-222222222222@reality.example.test:443" +
	"?encryption=none&security=reality&flow=xtls-rprx-vision&type=tcp" +
	"&sni=www.amazon.nl&fp=chrome"

func realityOptionsForSingleProxy(t *testing.T, input string) map[string]any {
	t.Helper()
	proxies, err := parseSubscriptionWithMihomo(input)
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	if len(proxies) != 1 {
		t.Fatalf("got %d proxies: %#v", len(proxies), proxies)
	}
	realityOptions, ok := proxies[0]["reality-opts"].(map[string]any)
	if !ok {
		t.Fatalf("reality-opts is not an object: %#v", proxies[0]["reality-opts"])
	}
	return realityOptions
}

func TestVlessRealityMlkemParamInjectsOption(t *testing.T) {
	input := realityMlkemTestBase +
		"&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
		"&sid=00112233&support-x25519mlkem768=1#MlkemNode"

	realityOptions := realityOptionsForSingleProxy(t, input)
	if value, ok := realityOptions["support-x25519mlkem768"].(bool); !ok || !value {
		t.Fatalf("support-x25519mlkem768 was not injected as true: %#v", realityOptions)
	}
}

func TestVlessRealityMlkemParamTrueKeywordAccepted(t *testing.T) {
	input := realityMlkemTestBase +
		"&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
		"&support-x25519mlkem768=true#MlkemNode"

	realityOptions := realityOptionsForSingleProxy(t, input)
	if value, ok := realityOptions["support-x25519mlkem768"].(bool); !ok || !value {
		t.Fatalf("support-x25519mlkem768=true was not injected: %#v", realityOptions)
	}
}

func TestVlessRealityMlkemParamWorksInBase64Subscription(t *testing.T) {
	body := realityMlkemTestBase +
		"&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
		"&support-x25519mlkem768=1#MlkemNode"
	input := base64.StdEncoding.EncodeToString([]byte(body))

	realityOptions := realityOptionsForSingleProxy(t, input)
	if value, ok := realityOptions["support-x25519mlkem768"].(bool); !ok || !value {
		t.Fatalf("support-x25519mlkem768 was not injected from base64 input: %#v", realityOptions)
	}
}

func TestVlessWithoutMlkemParamStaysUntouched(t *testing.T) {
	input := realityMlkemTestBase +
		"&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
		"&sid=00112233#PlainReality"

	realityOptions := realityOptionsForSingleProxy(t, input)
	if _, exists := realityOptions["support-x25519mlkem768"]; exists {
		t.Fatalf("support-x25519mlkem768 must stay absent without the parameter: %#v", realityOptions)
	}
}

func TestVlessMlkemParamWithoutRealityKeyIgnored(t *testing.T) {
	input := realityMlkemTestBase + "&support-x25519mlkem768=1#NoRealityKey"

	proxies, err := parseSubscriptionWithMihomo(input)
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	if _, exists := proxies[0]["reality-opts"]; exists {
		t.Fatalf("reality-opts must not be created for non-Reality nodes: %#v", proxies[0])
	}
}

func TestVlessMlkemParamTargetsOnlyFlaggedDuplicate(t *testing.T) {
	plain := realityMlkemTestBase +
		"&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA#PlainDuplicate"
	flagged := realityMlkemTestBase +
		"&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
		"&support-x25519mlkem768=1#FlaggedDuplicate"
	input := strings.Join([]string{plain, flagged, plain}, "\n")

	proxies, err := parseSubscriptionWithMihomo(input)
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	if len(proxies) != 3 {
		t.Fatalf("got %d proxies: %#v", len(proxies), proxies)
	}
	for index, proxy := range proxies {
		name, _ := proxy["name"].(string)
		realityOptions, ok := proxy["reality-opts"].(map[string]any)
		if !ok {
			t.Fatalf("proxy %d (%s) lost reality-opts: %#v", index, name, proxy)
		}
		_, injected := realityOptions["support-x25519mlkem768"]
		wantInjected := name == "FlaggedDuplicate" || name == "FlaggedDuplicate-1"
		if injected != wantInjected {
			t.Fatalf("proxy %d (%s) injected=%v, want %v: %#v",
				index, name, injected, wantInjected, realityOptions)
		}
	}
}

// Mihomo decodes a base64-encoded vless host before reading server and port.
// The override scan must mirror that or the flag is lost and line alignment
// drifts for every following link.
func TestVlessMlkemParamWithBase64Host(t *testing.T) {
	base64HostLink := "vless://22222222-2222-4222-8222-222222222222@" +
		"cmVhbGl0eS5leGFtcGxlLnRlc3Q6NDQz" +
		"?encryption=none&security=reality&type=tcp" +
		"&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA#Base64Host"
	flagged := realityMlkemTestBase +
		"&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" +
		"&support-x25519mlkem768=1#AfterBase64Host"
	input := strings.Join([]string{base64HostLink, flagged}, "\n")

	proxies, err := parseSubscriptionWithMihomo(input)
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	if len(proxies) != 2 {
		t.Fatalf("got %d proxies: %#v", len(proxies), proxies)
	}
	if server, _ := proxies[0]["server"].(string); server != "reality.example.test" {
		t.Fatalf("base64 host was not decoded by mihomo: %#v", proxies[0])
	}
	for _, proxy := range proxies {
		name, _ := proxy["name"].(string)
		realityOptions, _ := proxy["reality-opts"].(map[string]any)
		_, injected := realityOptions["support-x25519mlkem768"]
		if name == "AfterBase64Host" && !injected {
			t.Fatalf("flag after a base64-host link was not injected: %#v", proxy)
		}
		if name == "Base64Host" && injected {
			t.Fatalf("unflagged base64-host link was injected: %#v", proxy)
		}
	}
}
