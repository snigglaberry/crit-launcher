"""Resolving an old UWP Bedrock package URL through Windows Update's delivery service.

Modern releases are GDK builds that come straight from the GdkLinks catalog. Older
releases are Store packages; the Store's delivery service turns an update id into a
short-lived download URL. This is the same SOAP exchange BedrockLauncher uses.
"""

import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

from .catalog import PackageIdentity

NS_SOAP = "http://www.w3.org/2003/05/soap-envelope"
NS_ADDR = "http://www.w3.org/2005/08/addressing"
NS_SEC = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd"
NS_UTIL = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd"
NS_WUAUTH = "http://schemas.microsoft.com/msus/2014/10/WindowsUpdateAuthorization"
NS_WU = "http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService"

for _prefix, _uri in (("s", NS_SOAP), ("a", NS_ADDR), ("o", NS_SEC), ("u", NS_UTIL),
                      ("wuws", NS_WUAUTH), ("wu", NS_WU)):
    ET.register_namespace(_prefix, _uri)

SECURED_URL = "https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx/secured"
WU_ACTION = "http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService/"
WU_USER_AGENT = "Windows-Update-Agent/10.0.10011.16384 Client-Protocol/1.81"
DOWNLOAD_HOST_PREFIX = "http://tlu.dl.delivery.mp.microsoft.com/"

# The same synthetic Windows 10 1803 client profile BedrockLauncher sends.
DEVICE_ATTRIBUTES = (
    "E:BranchReadinessLevel=CBB&DchuNvidiaGrfxExists=1&ProcessorIdentifier=Intel64%20Family%206"
    "%20Model%2063%20Stepping%202&CurrentBranch=rs4_release&DataVer_RS5=1942&FlightRing=Retail"
    "&AttrDataVer=57&InstallLanguage=en-US&DchuAmdGrfxExists=1&OSUILocale=en-US&InstallationType=Client"
    "&FlightingBranchName=&Version_RS5=10&UpgEx_RS5=Green&GStatus_RS5=2&OSSkuId=48&App=WU"
    "&InstallDate=1529700913&ProcessorManufacturer=GenuineIntel&AppVer=10.0.17134.471"
    "&OSArchitecture=AMD64&UpdateManagementGroup=2&IsDeviceRetailDemo=0&HidOverGattReg=C%3A%5CWINDOWS"
    "%5CSystem32%5CDriverStore%5CFileRepository%5Chidbthle.inf_amd64_467f181075371c89"
    "%5CMicrosoft.Bluetooth.Profiles.HidOverGatt.dll&IsFlightingEnabled=0&DchuIntelGrfxExists=1"
    "&TelemetryLevel=1&DefaultUserRegion=244&DeferFeatureUpdatePeriodInDays=365&Bios=Unknown"
    "&WuClientVer=10.0.17134.471&PausedFeatureStatus=1&Steam=URL%3Asteam%20protocol&Free=8to16"
    "&OSVersion=10.0.17134.472&DeviceFamily=Windows.Desktop"
)


def _q(namespace: str, tag: str) -> str:
    return f"{{{namespace}}}{tag}"


def _sub(parent, tag: str, text: str | None = None, **attrib: str):
    element = ET.SubElement(parent, tag, attrib)
    if text is not None:
        element.text = text
    return element


def build_download_request(update_id: str, revision: int = 1) -> str:
    envelope = ET.Element(_q(NS_SOAP, "Envelope"))
    header = _sub(envelope, _q(NS_SOAP, "Header"))
    _sub(header, _q(NS_ADDR, "Action"), WU_ACTION + "GetExtendedUpdateInfo2").set(
        _q(NS_SOAP, "mustUnderstand"), "1")
    _sub(header, _q(NS_ADDR, "MessageID"), "urn:uuid:a68d4c75-ab85-4ca8-87db-136d281a2e28")
    _sub(header, _q(NS_ADDR, "To"), SECURED_URL).set(_q(NS_SOAP, "mustUnderstand"), "1")
    security = _sub(header, _q(NS_SEC, "Security"))
    security.set(_q(NS_SOAP, "mustUnderstand"), "1")
    timestamp = _sub(security, _q(NS_UTIL, "Timestamp"))
    _sub(timestamp, _q(NS_UTIL, "Created"), "2019-01-01T00:00:00.000Z")
    _sub(timestamp, _q(NS_UTIL, "Expires"), "2100-01-01T00:00:00.000Z")
    _sub(_sub(security, _q(NS_WUAUTH, "WindowsUpdateTicketsToken")),
         "TicketType", Name="AAD", Version="1.0", Policy="MBI_SSL")

    body = _sub(envelope, _q(NS_SOAP, "Body"))
    request = _sub(body, _q(NS_WU, "GetExtendedUpdateInfo2"))
    identity = _sub(_sub(request, _q(NS_WU, "updateIDs")), _q(NS_WU, "UpdateIdentity"))
    _sub(identity, _q(NS_WU, "UpdateID"), update_id)
    _sub(identity, _q(NS_WU, "RevisionNumber"), str(revision))
    _sub(_sub(request, _q(NS_WU, "infoTypes")), _q(NS_WU, "XmlUpdateFragmentType"), "FileUrl")
    _sub(request, _q(NS_WU, "deviceAttributes"), DEVICE_ATTRIBUTES)
    return ET.tostring(envelope, encoding="unicode")


def _post(url: str, payload: str) -> ET.Element:
    request = urllib.request.Request(
        url,
        data=payload.encode("utf-8"),
        headers={"Content-Type": "application/soap+xml; charset=utf-8",
                 "User-Agent": WU_USER_AGENT},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            text = response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as exc:
        text = exc.read().decode("utf-8", "replace")
    except (urllib.error.URLError, OSError) as exc:
        raise RuntimeError(f"Windows Update request failed: {exc}") from exc
    try:
        root = ET.fromstring(text)
    except ET.ParseError as exc:
        raise RuntimeError("Windows Update returned an unreadable response.") from exc
    fault = next(root.iter(_q(NS_SOAP, "Fault")), None)
    if fault is not None:
        code = next((element.text for element in fault.iter("ErrorCode")), None)
        raise RuntimeError(f"Windows Update returned an error (code {code or 'unknown'}).")
    return root


def resolve_download(update_id: str, expected: PackageIdentity | None = None) -> str:
    """Turn a Store update id into a package URL."""
    root = _post(SECURED_URL, build_download_request(update_id))
    for location in root.iter(_q(NS_WU, "FileLocation")):
        url = next(location.iter(_q(NS_WU, "Url")), None)
        if url is None or not url.text:
            continue
        if not url.text.startswith(DOWNLOAD_HOST_PREFIX):
            continue
        if expected is not None:
            identity = PackageIdentity.from_url(url.text)
            if identity is None or identity.name.lower() != expected.name.lower():
                continue
        return url.text
    raise RuntimeError("Windows Update did not return a download link for that Bedrock build.")
