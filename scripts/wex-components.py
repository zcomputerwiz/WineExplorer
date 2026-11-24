#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Administrator-only optional redistributables, with explicit vendor terms.

The unprivileged UI never runs winetricks. The fixed pkexec backend rechecks the
allowlist and the digest of root-owned license text before any download. Missing
vendor terms disable a component, rather than substituting a generic notice.
During OOBE, a root-owned coordinator serves the unprivileged wizard over a
stage-limited Unix domain socket verified via SO_PEERCRED.
"""
import argparse
from contextlib import contextmanager
import ctypes
import errno
import fcntl
import grp
import hashlib
import io
import json
import os
from pathlib import Path
import pwd
import re
import select
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import uuid

OPERATION_STATE = Path('/var/lib/wineexplorer/component-operations')


def active_oobe_stage(path):
    try:
        info = path.lstat()
        parent = path.parent.lstat()
        return (stat.S_ISDIR(parent.st_mode) and parent.st_uid == 0 and
                not parent.st_mode & 0o022 and stat.S_ISREG(info.st_mode) and
                info.st_uid == 0 and not info.st_mode & 0o022 and
                path.read_text(encoding='ascii').strip() == 'oobe')
    except (OSError, UnicodeError):
        return False
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

try:   # a parser that refuses entity tricks, when the system has it
    from defusedxml.ElementTree import fromstring as xml_fromstring
except ImportError:
    xml_fromstring = ET.fromstring

COMPONENTS = {
    'vcrun2022': 'Visual C++ 2015–2022 runtime',
    'corefonts': 'Microsoft core fonts (optional)',
    'tahoma': 'Microsoft Tahoma (optional)',
    'd3dcompiler_47': 'DirectX shader compiler (optional)',
    'xact': 'XACT audio libraries (optional)',
    'physx': 'NVIDIA PhysX (optional)',
    'webview2': 'Microsoft Edge WebView2 Runtime (optional)',
    'edge': 'Microsoft Edge (optional, needs the WebView2 Runtime)',
    'copilot': 'Microsoft Copilot (optional, needs the WebView2 Runtime)',
    'powershell': 'PowerShell 7 (optional)',
    'uwp-xaml': 'UWP XAML support, Uno Platform (optional)',
    'uwp-frameworks': 'UWP application frameworks, Microsoft (optional)',
}

COMPONENTS_METADATA = {
    'vcrun2022': {
        'title': 'Visual C++ 2015–2022 runtime',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs Visual C++ runtime libraries (vcruntime140, msvcp140, concrt140, vcamp140) required by many modern Windows applications and games.',
        'size': '~38 MiB (x86 and x64)',
        'reboot': False,
        'disabled': False,
        'disabled_reason': '',
        'terms_url': 'https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx',
        'terms_sha256': 'f1e3d56ceb2ad68aae0711b910375009e651ac5530fa0760f0dea6e81e54fae1',
        'terms_text_sha256': '993abe4df09446971c7a72f0b5c6cd1f0f69e57d574a2c9729582438708f3df0',
        'release': '14.44.35211.0',
        'execution_policy': 'vcrun2022-fixed-downloads-minimum-dlls-v2',
        'payloads': [
            {'name': 'vc_redist.x86.exe', 'architecture': 'x86', 'size': 13953392,
             'sha256': '0c09f2611660441084ce0df425c51c11e147e6447963c3690f97e0b25c55ed64',
             'url': 'https://download.visualstudio.microsoft.com/download/pr/990c720a-46ce-4dc7-87b9-c2eb615d34b9/0C09F2611660441084CE0DF425C51C11E147E6447963C3690F97E0B25C55ED64/VC_redist.x86.exe'},
            {'name': 'vc_redist.x64.exe', 'architecture': 'x64', 'size': 25635768,
             'sha256': 'cc0ff0eb1dc3f5188ae6300faef32bf5beeba4bdd6e8e445a9184072096b713b',
             'url': 'https://download.visualstudio.microsoft.com/download/pr/990c720a-46ce-4dc7-87b9-c2eb615d34b9/CC0FF0EB1DC3F5188AE6300FAEF32BF5BEEBA4BDD6E8E445A9184072096B713B/VC_redist.x64.exe'},
        ],
        'recipe_files': [
            ('vc_redist.x86.exe', '0c09f2611660441084ce0df425c51c11e147e6447963c3690f97e0b25c55ed64'),
            ('vc_redist.x64.exe', 'cc0ff0eb1dc3f5188ae6300faef32bf5beeba4bdd6e8e445a9184072096b713b'),
        ],
    },
    'corefonts': {
        'title': 'Microsoft TrueType Core Fonts',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs original Microsoft TrueType core fonts (Arial, Times New Roman, Courier New, Georgia, Verdana, Comic Sans, Trebuchet, Impact, Andale Mono, Webdings).',
        'size': '~4 MB',
        'reboot': False,
        'disabled': False,
        'disabled_reason': '',
        'terms_url': None,
        'recipe_files': [
            ('andale32.exe', '0524fe42951adc3a7eb870e32f0920313c71f170c859b5f770d82b4ee111e970'),
            ('arial32.exe', '85297a4d146e9c87ac6f74822734bdee5f4b2a722d7eaa584b7f2cbf76f478f6'),
            ('arialb32.exe', 'a425f0ffb6a1a5ede5b979ed6177f4f4f4fdef6ae7c302a7b7720ef332fec0a8'),
            ('comic32.exe', '9c6df3feefde26d4e41d4a4fe5db2a89f9123a772594d7f59afd062625cd204e'),
            ('courie32.exe', 'bb511d861655dde879ae552eb86b134d6fae67cb58502e6ff73ec5d9151f3384'),
            ('georgi32.exe', '2c2c7dcda6606ea5cf08918fb7cd3f3359e9e84338dc690013f20cd42e930301'),
            ('impact32.exe', '6061ef3b7401d9642f5dfdb5f2b376aa14663f6275e60a51207ad4facf2fccfb'),
            ('times32.exe', 'db56595ec6ef5d3de5c24994f001f03b2a13e37cee27bc25c58f6f43e8f807ab'),
            ('trebuc32.exe', '5a690d9bb8510be1b8b4fe49f1f2319651fe51bbe54775ddddd8ef0bd07fdac9'),
            ('verdan32.exe', 'c1cb61255e363166794e47664e2f21af8e3a26cb6346eb8d2ae2fa85dd5aad96'),
            ('webdin32.exe', '64595b5abc1080fba8610c5c34fab5863408e806aafe84653ca8575bed17d75a'),
        ],
        # Embedded terms route: applicable agreements are embedded in the
        # packages themselves (PE resource RCDATA/LICENSE/1033, CP1252).
        # Reviewing and accepting these terms enables verified offline installation.
        'review': {
            'mode': 'embedded-package-terms',
            'policy': 'embedded-review-v1',
            'url_prefix': 'https://downloads.sourceforge.net/corefonts/',
            'host_suffixes': ('sourceforge.net',),
            'resource': {'type': 10, 'name': 'LICENSE', 'language': 1033, 'encoding': 'cp1252'},
            # name: (package bytes, resource bytes, resource SHA-256). The package
            # SHA-256 values are the recipe_files above. The eleven resources are
            # NOT assumed identical; every distinct agreement is shown.
            'packages': {
                'andale32.exe': (198384, 4920, 'e025f4a7933977c1d98665616fdded6130b7a130f8eca18d72d18d8b71fd8d57'),
                'arial32.exe': (554208, 4925, '877a177fe9b15afdf4aac68ec9d6c8366c8b0ca2d2dfe104f5df388ed1cf026b'),
                'arialb32.exe': (168176, 4920, 'e025f4a7933977c1d98665616fdded6130b7a130f8eca18d72d18d8b71fd8d57'),
                'comic32.exe': (246008, 4920, 'e025f4a7933977c1d98665616fdded6130b7a130f8eca18d72d18d8b71fd8d57'),
                'courie32.exe': (646368, 4925, '877a177fe9b15afdf4aac68ec9d6c8366c8b0ca2d2dfe104f5df388ed1cf026b'),
                'georgi32.exe': (392440, 4920, 'e025f4a7933977c1d98665616fdded6130b7a130f8eca18d72d18d8b71fd8d57'),
                'impact32.exe': (173288, 4920, 'e025f4a7933977c1d98665616fdded6130b7a130f8eca18d72d18d8b71fd8d57'),
                'times32.exe': (661728, 4925, '877a177fe9b15afdf4aac68ec9d6c8366c8b0ca2d2dfe104f5df388ed1cf026b'),
                'trebuc32.exe': (357200, 4921, '6c265edc5dd8be4a0ac1dfa7bf474e7b68c331fbad5a441b8099d3f78d071d3f'),
                'verdan32.exe': (351992, 4920, 'e025f4a7933977c1d98665616fdded6130b7a130f8eca18d72d18d8b71fd8d57'),
                'webdin32.exe': (185072, 4920, 'e025f4a7933977c1d98665616fdded6130b7a130f8eca18d72d18d8b71fd8d57'),
            },
            'rendered_sha256': '832fa599d8595115775343f4fbbffbd6e616d1d79a25e23a9c25c9d7b6ddfba9',
        },
    },
    'tahoma': {
        'title': 'Microsoft Tahoma font',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs Microsoft Tahoma and Tahoma Bold TrueType fonts from the official Microsoft Internet Explorer font package.',
        'size': '~410 KB',
        'reboot': False,
        'disabled': True,
        'disabled_reason': 'Historical IE language-pack terms and permitted Tahoma use require package-specific verification',
        'terms_url': None,
        'recipe_files': [
            ('IELPKTH.CAB', 'c1be3fb8f0042570be76ec6daa03a99142c88367c1bc810240b85827c715961a'),
        ],
    },
    'd3dcompiler_47': {
        'title': 'DirectX Shader Compiler (d3dcompiler_47)',
        'publisher': 'Microsoft Corporation',
        'description': 'DirectX HLSL shader compiler library required by DirectX 11, DirectX 12, and Vulkan translation layers (DXVK/VKD3D). Wine builtin compiler remains default.',
        'size': '~4.2 MB',
        'reboot': False,
        'disabled': False,
        'disabled_reason': '',
        'terms_url': None,
        'recipe_files': [
            ('sdksetup.exe', '5107822a5a99bcdef4c7b7c7eea218425692f0185750b0d4fafd441034a7486b'),
            ('2630bae9681db6a9f6722366f47d055c.cab', 'd0440eb81c532dc23639c0c63f2fcde9deddb23bb4cce01c19ac6b96cc3e269d'),
            ('61d57a7a82309cd161a854a6f4619e52.cab', 'f736e161547095bb8d98c636b85fdfeb4070fefeee3c3745db3ce88f6eb1d9de'),
        ],
        'review': {
            'mode': 'embedded-package-terms',
            'policy': 'embedded-review-v1',
            'url_prefix': 'https://download.microsoft.com/download/B/0/C/B0C80BA3-8AD6-4958-810B-6882485230B5/standalonesdk/',
            'urls': {
                'sdksetup.exe': 'https://download.microsoft.com/download/B/0/C/B0C80BA3-8AD6-4958-810B-6882485230B5/standalonesdk/sdksetup.exe',
                '2630bae9681db6a9f6722366f47d055c.cab': 'https://download.microsoft.com/download/B/0/C/B0C80BA3-8AD6-4958-810B-6882485230B5/standalonesdk/Installers/2630bae9681db6a9f6722366f47d055c.cab',
                '61d57a7a82309cd161a854a6f4619e52.cab': 'https://download.microsoft.com/download/B/0/C/B0C80BA3-8AD6-4958-810B-6882485230B5/standalonesdk/Installers/61d57a7a82309cd161a854a6f4619e52.cab',
            },
            'host_suffixes': ('microsoft.com',),
            'packages': {
                'sdksetup.exe': (998056, 64354, '7606715231cab510135bb271836677797189c64cfacc59b8b444e427fecace78'),
                '2630bae9681db6a9f6722366f47d055c.cab': (1488323, 0, ''),
                '61d57a7a82309cd161a854a6f4619e52.cab': (1776364, 0, ''),
            },
            'rendered_sha256': '5b575dc86b5e04198a8915dabe81b987daae47be007ad42897bc57a5d92a5f43',
        },
    },
    'xact': {
        'title': 'Microsoft XACT audio engine',
        'publisher': 'Microsoft Corporation',
        'description': 'Cross-Platform Audio Creation Tool (XACT) runtime libraries for game audio.',
        'size': '~95 MB',
        'reboot': False,
        'disabled': True,
        'disabled_reason': 'DirectX package terms acquisition and XACT architecture-specific validation pending',
        'terms_url': None,
        'recipe_files': [
            ('directx_Jun2010_redist.exe', '8746ee1a84a083a90e37899d71d50d5c7c015e69688a466aa80447f011780c0d'),
        ],
    },
    'physx': {
        'title': 'NVIDIA PhysX System Software',
        'publisher': 'NVIDIA Corporation',
        'description': 'Physics simulation engine software required by PhysX-enabled games.',
        'size': '~27 MB',
        'reboot': False,
        'disabled': True,
        'disabled_reason': 'Pinned recipe predates Wine 9.10 fix; updated package, terms and product-Wine validation pending',
        'terms_url': None,
        'recipe_files': [
            ('PhysX_9.21.0713_SystemSoftware.exe', '26d62c5c347c15cb27c3be92bf10706113511b48b28aecc09f61ee58b3b62778'),
        ],
    },
    # Not a winetricks verb: Microsoft's Evergreen standalone installer is run
    # directly and silently. Programs such as Copilot embed WebView2 and look for
    # the runtime this installer registers under EdgeUpdate\Clients.
    'webview2': {
        'title': 'Microsoft Edge WebView2 Runtime',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs the Evergreen WebView2 Runtime that programs embed for web content and Microsoft account sign-in (for example Copilot and Office).',
        'size': '~205 MiB',
        'reboot': False,
        'disabled': False,
        'disabled_reason': '',
        'terms_url': 'https://developer.microsoft.com/microsoft-edge/api/eula/webview2?locale=en-us',
        'terms_format': 'webview2-json',
        'terms_sha256': '821657eb0994850b0ea86b53242c52a22016e45859fee74bcd456dd77c0ba18e',
        'terms_text_sha256': 'ea872143d6deec90961d3fcfe303c199b3c8fcbe168fed5a6447ce916e37aa53',
        'release': '157.0.4341.60',
        'execution_policy': 'webview2-evergreen-standalone-silent-v1',
        'payloads': [
            {'name': 'MicrosoftEdgeWebView2RuntimeInstallerX64.exe', 'architecture': 'x64', 'size': 215116512,
             'sha256': 'd3cbe8fedb104282d745fff106993efead51ae94f5595a4e75804446a974f9c3',
             'host': 'msedge.sf.dl.delivery.mp.microsoft.com',
             'url': 'https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/52cfd0a8-f256-48d4-9b18-467f2b3cda78/MicrosoftEdgeWebView2RuntimeInstallerX64.exe'},
        ],
        'client_guid': '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
        'recipe_files': [],
    },
    # Microsoft's enterprise MSIs, installed machine-wide by msiexec as session 0's account (SYSTEM), as Windows runs
    # their DoInstall chain (docs/KNOWN_BUGS.md B104). WebView2 goes first: the Copilot app embeds it, and with it
    # installed Edge Update already has an app when the MSI's updater registers the browser. Their terms are web
    # pages: only the text of the page's main element, from terms_start on, is pinned (the markup around it changes).
    'edge': {
        'title': 'Microsoft Edge',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs the Microsoft Edge web browser for all users from Microsoft\'s enterprise package. '
                       'Needs the WebView2 Runtime component.',
        'size': '~206 MiB',
        'reboot': False,
        'disabled': False,
        'disabled_reason': '',
        'terms_url': 'https://explore.microsoft.com/en-us/edge/terms',
        'terms_format': 'html',
        'terms_start': 'MICROSOFT SOFTWARE LICENSE TERMS',
        'terms_sha256': '1acc9dbc7f011f9bbf49e24705ac7beb56e70031e7cbe2e8babd85cef2f1c7ff',
        'terms_text_sha256': '1acc9dbc7f011f9bbf49e24705ac7beb56e70031e7cbe2e8babd85cef2f1c7ff',
        'release': '157.0.4341.60',
        'execution_policy': 'edgeupdate-msi-machine-v1',
        'payloads': [
            {'name': 'MicrosoftEdgeEnterpriseX64.msi', 'architecture': 'x64', 'size': 215552000,
             'sha256': 'b86b238035c47673e1c70589a28f2e0961e8cabdd3ff25edbb1e52239d7ecd76',
             'host': 'msedge.sf.dl.delivery.mp.microsoft.com',
             'url': 'https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/277044fc-f1ea-4102-a313-b58e7fc47c6c/MicrosoftEdgeEnterpriseX64.msi'},
        ],
        'client_guid': '{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}',
        'artifacts': ['Edge/Application/msedge.exe', 'Edge/Application/{release}/msedge.dll'],
        'requires': ['webview2'],
        'recipe_files': [],
    },
    'copilot': {
        'title': 'Microsoft Copilot',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs the Microsoft Copilot app for all users from Microsoft\'s enterprise package. '
                       'Needs the WebView2 Runtime component; signing in uses the Microsoft account broker.',
        'size': '~406 MiB',
        'reboot': False,
        'disabled': False,
        'disabled_reason': '',
        'terms_url': 'https://www.microsoft.com/en-us/microsoft-copilot/for-individuals/termsofuse',
        'terms_format': 'html',
        'terms_start': 'Microsoft Copilot Supplemental Terms of Use',
        'terms_sha256': '4ff50cf1129b8db34fc1162383c10904f73d1d60dd655b6e2dcd19ff7f636cb5',
        'terms_text_sha256': '4ff50cf1129b8db34fc1162383c10904f73d1d60dd655b6e2dcd19ff7f636cb5',
        'release': '158.0.4370.33',
        'execution_policy': 'edgeupdate-msi-machine-v1',
        'payloads': [
            {'name': 'CopilotStableEnterpriseX64.msi', 'architecture': 'x64', 'size': 425648128,
             'sha256': '565b5cb202c22dee664193a77300d09063c9a754640f2d27f4ae43fb71eff4db',
             'host': 'msedge.sf.dl.delivery.mp.microsoft.com',
             'url': 'https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/e314a6b7-611f-4d3a-b9c8-955a623605e8/CopilotStableEnterpriseX64.msi'},
        ],
        'client_guid': '{C50565E9-CCCF-44B4-BA15-5AC5C6569197}',
        'artifacts': ['Copilot/Application/copilotapp.exe', 'Copilot/Application/{release}/Installer/setup.exe'],
        'requires': ['webview2'],
        'recipe_files': [],
    },
    # Microsoft's open-source PowerShell (MIT), the release zip unpacked as it is; no installer program runs. The
    # WexComputer module of the image (Add-Computer, Remove-Computer, Test-ComputerSecureChannel,
    # Reset-ComputerMachinePassword, which PowerShell 7 lacks) is added under Modules/. Needs a console window:
    # with plain pipes pwsh.exe exits without running anything.
    'powershell': {
        'title': 'PowerShell 7',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs PowerShell 7, the command shell and scripting language, with the WexComputer module '
                       '(Add-Computer, Remove-Computer, Test-ComputerSecureChannel, Reset-ComputerMachinePassword). '
                       'It runs in a console window; the computer cmdlets need the image\'s domain join support. No '
                       'installer program runs: the release archive is verified and unpacked.',
        'size': '~110 MiB download, ~256 MiB installed (x64)',
        'reboot': False,
        'disabled': False,
        'disabled_reason': '',
        # the MIT licence of the release (the archive carries the same text and its third-party notices)
        'terms_url': '',
        'terms_documents': [
            {'title': 'PowerShell (MIT License)', 'format': 'txt',
             'url': 'https://raw.githubusercontent.com/PowerShell/PowerShell/v7.7.0/LICENSE.txt',
             'sha256': '7c77a44a8acd9b41fdc209864a8016b3d430b5d0e09309818d5b7444336df744'},
        ],
        'terms_sha256': '42fd29451073f3d7c5bf0bc54c86770a63062e6f9a56a44e5b018019ac705673',
        'terms_text_sha256': '77acdaad477970da4e2fdb5f3f59bbbfc5d6dfbba71f525ddd0c12be671f924e',
        'release': '7.7.0',
        'execution_policy': 'powershell-extract-v1',
        'payloads': [
            {'name': 'PowerShell-7.7.0-win-x64.zip', 'architecture': 'x64', 'size': 115330228,
             'sha256': '7c74e03b20a75b59715847439d046a6214b1863ee79b187872980f139bb4a05f',
             'url': 'https://github.com/PowerShell/PowerShell/releases/download/v7.7.0/PowerShell-7.7.0-win-x64.zip'},
        ],
        'recipe_files': [
            ('PowerShell-7.7.0-win-x64.zip', '7c74e03b20a75b59715847439d046a6214b1863ee79b187872980f139bb4a05f'),
        ],
    },
    'uwp-xaml': {
        'title': 'UWP XAML support (Uno Platform)',
        'publisher': 'WineExplorer contributors and the Uno Platform',
        'description': "Lets UWP applications that use XAML (Windows.UI.Xaml) show their user interface: Wine's "
                       'windows.ui.xaml.dll serves these classes from this component, built on the Uno Platform '
                       'and the .NET runtime. No winetricks and no installer program run: the archive is verified, '
                       'unpacked and registered.',
        'size': '~57 MiB download, ~140 MiB installed (x64)',
        'reboot': False,
        'disabled': False,
        'disabled_reason': '',
        # Apache-2.0 (XamlShim, Uno) followed by every bundled third-party licence (.NET, SkiaSharp, HarfBuzz, ICU)
        'terms_url': 'https://github.com/zcomputerwiz/uno-uwp/releases/download/wex-xamlshim-v0.3.3/TERMS.txt',
        'terms_sha256': 'b3f562f5335eae2d69ef70b1d2061b08b41fa59d8e1f5292486639fb4170880c',
        'terms_text_sha256': 'b3f562f5335eae2d69ef70b1d2061b08b41fa59d8e1f5292486639fb4170880c',
        'release': '0.3.3',
        'execution_policy': 'uwp-xaml-extract-register-v1',
        'payloads': [
            {'name': 'xamlshim-0.3.3-win-x64.zip', 'architecture': 'x64', 'size': 59813000,
             'sha256': '972a8c415eec759aa7aa8f5a752a8b661a8bf8c24885e15f6b9df337fb38780f',
             'url': 'https://github.com/zcomputerwiz/uno-uwp/releases/download/wex-xamlshim-v0.3.3/xamlshim-0.3.3-win-x64.zip'},
        ],
        'recipe_files': [
            ('xamlshim-0.3.3-win-x64.zip', '972a8c415eec759aa7aa8f5a752a8b661a8bf8c24885e15f6b9df337fb38780f'),
        ],
    },
    # Microsoft's framework packages that UWP applications declare as dependencies (the Windows Calculator 11.2607
    # needs these five), taken from Microsoft's own versioned downloads: nuget.org packages of Microsoft and Visual
    # Studio's payload catalog. Each container and each package in it is pinned by size and SHA-256.
    'uwp-frameworks': {
        'title': 'UWP application frameworks (Microsoft)',
        'publisher': 'Microsoft Corporation',
        'description': 'Framework packages that UWP applications depend on: Windows UI Library 2.8, the .NET Native '
                       'framework and runtime 2.2, and the Visual C++ runtime libraries for UWP and for desktop '
                       'bridge applications. They are verified, unpacked and registered for every user; no '
                       'installer program runs.',
        'size': '~238 MiB download, ~45 MiB installed (x64)',
        'reboot': False,
        'disabled': False,
        'disabled_reason': '',
        # Microsoft's licence terms for each kind of package, as Microsoft publishes them: the WinUI licence inside
        # the WinUI 2.8.7 package (read by range requests), the .NET Native (UWP) licence on Microsoft's GitHub, and
        # the Visual C++ runtime licence (the document vcrun2022 shows too; VCLibs is that runtime for UWP).
        'terms_url': '',
        'terms_documents': [
            {'title': 'Microsoft Windows UI Library (Microsoft.UI.Xaml 2.8)', 'format': 'zip-member',
             'url': 'https://api.nuget.org/v3-flatcontainer/microsoft.ui.xaml/2.8.7/microsoft.ui.xaml.2.8.7.nupkg',
             'size': 20314649, 'member': 'license.txt',
             'sha256': '394500b97c5f2f7cbb18fd9aa3a9bcc4e11f48b6423aa1e48f3e9c1bbe4a9fea'},
            {'title': '.NET Native for UWP (Microsoft.NET.Native.Framework and Runtime 2.2)', 'format': 'txt',
             'url': 'https://raw.githubusercontent.com/Microsoft/dotnet/master/releases/UWP/LICENSE.TXT',
             'sha256': 'ac3997606b407ffb3843dab93b62cc20976841932159cb062684b840dd2c2a5e'},
            {'title': 'Microsoft Visual C++ runtime (Microsoft.VCLibs.140.00 and .UWPDesktop)', 'format': 'docx',
             'url': 'https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx',
             'sha256': 'f1e3d56ceb2ad68aae0711b910375009e651ac5530fa0760f0dea6e81e54fae1'},
        ],
        'terms_sha256': '9dab9215fb4673cf9c785c379ea74c33cb7ae66927ecee21281e541425072bfb',     # the document set
        'terms_text_sha256': '0a57dd992785db77dbd9b6924ebd8294859d25cc6890bb1397da9615e4dedd3c',
        'release': 'calculator-11.2607',
        'execution_policy': 'uwp-frameworks-extract-register-v1',
        'publisher_name': 'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US',
        'containers': [
            {'name': 'microsoft.ui.xaml.2.8.7.nupkg', 'format': 'zip', 'size': 20314649,
             'sha256': '79207b10fe243eb1a8dcdc29beceba2f472f145ed31f147f7ae3f43b0659c9f7',
             'url': 'https://api.nuget.org/v3-flatcontainer/microsoft.ui.xaml/2.8.7/microsoft.ui.xaml.2.8.7.nupkg',
             'packages': [
                 {'member': 'tools/AppX/x64/Release/Microsoft.UI.Xaml.2.8.appx', 'size': 4932225,
                  'sha256': '1421dba0b7f1da87e6ad52e17c25d38bcd2159bd7f91c85034cfa837238abaaa',
                  'name': 'Microsoft.UI.Xaml.2.8', 'version': '8.2501.31001.0', 'architecture': 'x64'}]},
            {'name': 'runtime.win10-x64.microsoft.net.native.sharedlibrary.2.2.8-rel-29512-01.nupkg', 'format': 'zip',
             'size': 96883491, 'sha256': 'ea5bab0c694f2c2c3afeb678d498024fcf6fcb4516ffbec2b2f4c78c4e926bfe',
             'url': 'https://api.nuget.org/v3-flatcontainer/runtime.win10-x64.microsoft.net.native.sharedlibrary/'
                    '2.2.8-rel-29512-01/runtime.win10-x64.microsoft.net.native.sharedlibrary.2.2.8-rel-29512-01.nupkg',
             'packages': [
                 {'member': 'tools/SharedLibrary/ret/Native/Microsoft.NET.Native.Framework.2.2.appx', 'size': 5204216,
                  'sha256': 'c195047f4af9c3ff3d92c40e55a901cf7cfa08a679b8ca2543bb104bab2f7ff1',
                  'name': 'Microsoft.NET.Native.Framework.2.2', 'version': '2.2.29512.0', 'architecture': 'x64'}]},
            {'name': 'runtime.win10-x64.microsoft.net.native.compiler.2.2.12-rel-33220-00.nupkg', 'format': 'zip',
             'size': 56533936, 'sha256': 'f80ccb5cd10159a4251f399c1940b7d67ad3c081117e923aa51249b11813ae98',
             'url': 'https://api.nuget.org/v3-flatcontainer/runtime.win10-x64.microsoft.net.native.compiler/'
                    '2.2.12-rel-33220-00/runtime.win10-x64.microsoft.net.native.compiler.2.2.12-rel-33220-00.nupkg',
             'packages': [
                 {'member': 'tools/Runtime/x64/Microsoft.NET.Native.Runtime.2.2.appx', 'size': 244530,
                  'sha256': '27fae660d8c609e020703bb463b6601e62811dc75db19975bcb7d68d959b527d',
                  'name': 'Microsoft.NET.Native.Runtime.2.2', 'version': '2.2.28604.0', 'architecture': 'x64'}]},
            # Visual Studio 2022 17.14, Microsoft.VisualCpp.CRT.Appx.Msi (14.44.35208), cab1.cab
            {'name': 'vc-crt-appx-14.44.35208-cab1.cab', 'format': 'cab', 'size': 9117314,
             'sha256': 'dd4dbe036d235dd902ef41f2cb3ec7ce7ab1ebf93e6cc26fb6d7283301188e5a',
             'url': 'https://download.visualstudio.microsoft.com/download/pr/d2fa077f-a814-4fb2-b903-1fca7658d17e/'
                    'dd4dbe036d235dd902ef41f2cb3ec7ce7ab1ebf93e6cc26fb6d7283301188e5a/cab1.cab',
             'packages': [
                 {'member': 'Microsoft.VCLibs.x64.14.00.appx', 'size': 896581,
                  'sha256': '9c17b521f9d690a1f504da5108ed6eec5669eb3a8fd1331eef43e40d84e74283',
                  'name': 'Microsoft.VCLibs.140.00', 'version': '14.0.33519.0', 'architecture': 'x64'}]},
            # Visual Studio 2022 17.14, Microsoft.VisualCpp.CRT.Appx.Desktop.140.Msi (14.44.35208), cab1.cab
            {'name': 'vc-crt-appx-desktop-140-14.44.35208-cab1.cab', 'format': 'cab', 'size': 67599114,
             'sha256': '2a18edcb42dfb466650bea57b02ac25bffb4dcb5aa1d72cce2ff0cdc50d72139',
             'url': 'https://download.visualstudio.microsoft.com/download/pr/d2fa077f-a814-4fb2-b903-1fca7658d17e/'
                    '2a18edcb42dfb466650bea57b02ac25bffb4dcb5aa1d72cce2ff0cdc50d72139/cab1.cab',
             'packages': [
                 {'member': 'Microsoft.VCLibs.x64.14.00.Desktop.appx', 'size': 6757465,
                  'sha256': '077a3d1a5d0622bd3004dca85f5e192d6e98ec79b83d4aa06766759ea6c09c3d',
                  'name': 'Microsoft.VCLibs.140.00.UWPDesktop', 'version': '14.0.33728.0', 'architecture': 'x64'}]},
        ],
    },
}

TERMS = Path('/etc/wineexplorer/component-licenses')
MACHINE = Path('/var/lib/wineexplorer/machine')
CACHE = Path('/var/cache/winetricks')
BACKEND = '/usr/local/bin/wex-components'
WINETRICKS_PATH = Path('/usr/bin/winetricks')
APPROVED_WINETRICKS_VERSION = '20250102'
APPROVED_WINETRICKS_SHA256 = '9df4af038f04c753f3b0b02817cf60a51d8eac32357d070c45dfd139b93bd3f5'

# Winetricks must not replace these Wine implementations, even as an indirect
# side effect of an allowed verb such as vcrun2022. Native d3dcompiler is optional.
PROTECTED = ('dwrite', 'gdiplus', 'usp10', 'riched20', 'mf', 'mfplat', 'mfreadwrite',
             'msxml', 'msxml2', 'msxml3', 'msxml4', 'msxml6', 'ucrtbase', 'wininet',
             'crypt32', 'kernel32', 'kernelbase', 'ntdll', 'user32', 'gdi32',
             'advapi32', 'shell32', 'ole32', 'combase')


def validate_verb(verb):
    if verb not in COMPONENTS:
        raise ValueError(f'Unsupported component: {verb}')
    return verb


def validate_winetricks_integrity(path=None):
    winetricks = path or WINETRICKS_PATH
    if not winetricks.is_file():
        raise ValueError(f'Winetricks executable missing at {winetricks}')
    if winetricks.is_symlink():
        raise ValueError('Refusing symlinked winetricks executable')
    stat = winetricks.stat()
    if stat.st_uid != 0 and (os.getuid() == 0 or stat.st_uid != os.getuid()):
        raise ValueError('Winetricks executable must be root-owned and not writable by others')
    if stat.st_mode & 0o022:
        raise ValueError('Winetricks executable must not be writable by group or others')
    digest = hashlib.sha256(winetricks.read_bytes()).hexdigest()
    if digest != APPROVED_WINETRICKS_SHA256:
        raise ValueError(f'Winetricks integrity violation: expected {APPROVED_WINETRICKS_SHA256}, got {digest}')


def validate_recipe_files(verb, path=None):
    if COMPONENTS_METADATA.get(verb, {}).get('execution_policy') in (UWP_XAML_POLICY, UWP_FRAMEWORKS_POLICY,
                                                                      POWERSHELL_POLICY):
        return   # no winetricks recipe: the pinned payload is checked when it is downloaded and before it is used
    if verb == 'vcrun2022':
        approved_runtime_recipe(path)
        return
    if verb == 'd3dcompiler_47':
        approved_d3dcompiler_recipe(path)
        return
    if verb in EDGEUPDATE_APPS:
        return  # no winetricks recipe: one pinned installer, verified before it runs
    winetricks = path or WINETRICKS_PATH
    meta = COMPONENTS_METADATA.get(verb, {})
    expected_files = meta.get('recipe_files', [])
    if not expected_files:
        raise ValueError(f'No approved recipe files configured for {verb}')
    content = winetricks.read_text(encoding='utf-8', errors='replace')
    if verb == 'corefonts':
        dependencies = ('andale', 'arial', 'comicsans', 'courier', 'georgia', 'impact',
                        'times', 'trebuchet', 'verdana', 'webdings')
        def body(name):
            match = re.search(r'^load_' + name + r'\(\)\s*\n\{\n(.*?)^\}', content, re.M | re.S)
            if not match:
                raise ValueError('Recipe integrity violation: missing ' + name)
            return match[1]
        calls = re.findall(r'^\s*w_call ([a-z0-9_]+)\s*$', body('corefonts'), re.M)
        if calls != list(dependencies):
            raise ValueError('Recipe integrity violation: changed Core Fonts dependency closure')
        downloads = []
        for dependency in dependencies:
            for url, digest in re.findall(r'^\s*w_download_to corefonts "([^"\n]+)" ([0-9a-f]{64})\s*$',
                                           body(dependency), re.M):
                downloads.append((url.rsplit('/', 1)[-1], digest))
        if downloads != expected_files:
            raise ValueError('Recipe integrity violation: Core Fonts package manifest differs from dependency downloads')
        return
    for fname, sha in expected_files:
        if fname not in content or sha not in content:
            raise ValueError(f'Recipe integrity violation: {verb} requires {fname} with hash {sha}')


# Hashes of individual minimum-runtime members from the exact accepted packages.
# No vendor file contents are distributed here. Other cabinets (including ARM64
# and MFC) are deliberately outside this replacement manifest.
RUNTIME_ARTIFACTS = {
    'x86': {
        'concrt140': 'a2b04c69868b08a600518ebbad1661d38de4dd0e0a8e638c22d4641d410e50b1',
        'msvcp140': '97e71a3c697fb1ab3af777db39009e4602525e8c3aff0837ecc7031cba188165',
        'msvcp140_1': '01a58552686d0c6b9fd67a7ac81169fc0013aedf59a8efdc611f94907489a31c',
        'msvcp140_2': '2a5365e03cc6e78b408dfca81537f3734bf3d7cf9cc13ae33204dfbc641dbe43',
        'msvcp140_atomic_wait': 'c4a2bae74e008a5bb2445f9078c14df9dcb03267248dfa64e7d04cdc8af5208e',
        'msvcp140_codecvt_ids': 'a5cbecfd9b8892ec9a60e30fef7ef2158032c6d91efdfbaca87c082bcd48bfa9',
        'vcamp140': '3df343f3951c63caffa1bd04e93728483efdfc67220d2c03a81ff36563dfbdf2',
        'vccorlib140': '89ed5a6877a87ec3ec9fe4a0fb14dc950599f0b67318e746084a0c333a6c865f',
        'vcomp140': '91cbb2dbb3c4f279e5b660049b0c86ba85de845931a182ad86c45121bb013d6d',
        'vcruntime140': 'a9d7732a3c3de09f0601a7fa2555093dcb49f08e2fc62a86091461f0f38393ed',
        'vcruntime140_threads': 'bb2b1d16187562376fc888059e2cbbf26c5404f6a4bb7197d1d01b970f199667',
    },
    'x64': {
        'concrt140': '2405355f0a58067b258f8df33c327e3a3d716eaac5a3a5aebb757842d85bd376',
        'msvcp140': '0f885b509a685d2bbfa652fed26b5fb31d88fbdab0a978c641d1c7b8aa460aa9',
        'msvcp140_1': 'bfad5aef4c63a669e3c140655cdfdf395b6c979b400a447bd5dcb65ed8826c3d',
        'msvcp140_2': '3ea06f0ee098b4823cb79599df3780e7f23cce52c19aac31d2a0d47efe33a5e9',
        'msvcp140_atomic_wait': '640b2aefced484d0368eea5bdd06addd0658a3a70a49256e560d6923b404a479',
        'msvcp140_codecvt_ids': 'f2069a52880ec885ee7f0511186100eb7fada0411a2b4948fafea7735b878a18',
        'vcamp140': '52af68f35476bd03d7b97752b01bef358ffdda568d150057d6ce809f77c4f704',
        'vccorlib140': '19839407c3fdbc824e5bce189bf68ddf8097f12ec28b757797ffa0415c144ddd',
        'vcomp140': '55aba23cdcd6484fbb06f4155b8ca75adfce7a881f10afd0c49457165e677164',
        'vcruntime140': 'd5e4d9a3e835fa679450145d6a7d94e36573a509317111904d9b3712c30d9066',
        'vcruntime140_1': '1f2d41c4aa5db0bc33ebf7b66d72943a817d7ce6cbe880502a9403823633093f',
        'vcruntime140_threads': '219915cf20822f34d5e7c1fdd4e21ae7f3396881096c51036225fb8f84b47afa',
    },
}


def runtime_artifact_paths(machine):
    for arch, directory in (('x86', 'syswow64'), ('x64', 'system32')):
        for name, digest in RUNTIME_ARTIFACTS[arch].items():
            yield machine / 'drive_c/windows' / directory / (name + '.dll'), digest


def verify_runtime_artifacts(machine):
    """Read only, through descriptors (a link, a FIFO or a device in a name is refused, never followed or waited on)."""
    windows = machine / 'drive_c/windows'
    try:
        root = machine_open_for_reading(machine, ('drive_c', 'windows'))
    except ValueError:
        raise ValueError('Approved runtime artifact missing or changed: ' + str(windows)) from None
    try:
        for path, digest in runtime_artifact_paths(machine):
            try:
                fd = machine_open_regular(root, list(path.relative_to(windows).parts))
                try:
                    actual = machine_fd_sha256(fd)
                finally:
                    os.close(fd)
            except ValueError:
                actual = None
            if actual != digest:
                raise ValueError('Approved runtime artifact missing or changed: ' + str(path))
    finally:
        os.close(root)


COREFONTS_ARTIFACTS = {
    'andalemo.ttf': (105468, '48d9bc613917709d3b0e0f4a6d4fe33a5c544c5035dffe9e90bc11e50e822071'),
    'arial.ttf': (275572, '35c0f3559d8db569e36c31095b8a60d441643d95f59139de40e23fada819b833'),
    'arialbd.ttf': (286620, '4044aa6b5bebbc36980206b45b0aaaaa5681552a48bcadb41746d5d1d71fd7b4'),
    'arialbi.ttf': (224692, '2f371cd9d96b3ac544519d85c16dc43ceacdfcea35090ee8ddf3ec5857c50328'),
    'ariali.ttf': (206132, '70ade233175a6a6675e4501461af9326e6f78b1ffdf787ca0da5ab0fc8c9cfd6'),
    'ariblk.ttf': (117028, 'dad7c04acb26e23dfe4780e79375ca193ddaf68409317e81577a30674668830e'),
    'comic.ttf': (126364, 'b82c53776058f291382ff7e008d4675839d2dc21eb295c66391f6fb0655d8fc0'),
    'comicbd.ttf': (111476, '873361465d994994762d0b9845c99fc7baa2a600442ea8db713a7dd19f8b0172'),
    'cour.ttf': (302688, '6715838c52f813f3821549d3f645db9a768bd6f3a43d8f85a89cb6875a546c61'),
    'courbd.ttf': (311508, 'edf8a7c5bfcac2e1fe507faab417137cbddc9071637ef4648238d0768c921e02'),
    'courbi.ttf': (234788, '66dbfa20b534fba0e203da140fec7276a45a1069e424b1b9c35547538128bbe8'),
    'couri.ttf': (244156, 'f3f6b09855b6700977e214aab5eb9e5be6813976a24f894bd7766e92c732fbe1'),
    'georgia.ttf': (142964, '7d0bb20c632bb59e81a0885f573bd2173f71f73204de9058feb68ce032227072'),
    'georgiab.ttf': (139584, '82d2fbadb88a8632d7f2e8ad50420c9fd2e7d3cbc0e90b04890213a711b34b93'),
    'georgiai.ttf': (156668, '1523f19bda6acca42c47c50da719a12dd34f85cc2606e6a5af15a7728b377b60'),
    'georgiaz.ttf': (158796, 'c983e037d8e4e694dd0fb0ba2e625bca317d67a41da2dc81e46a374e53d0ec8a'),
    'impact.ttf': (136076, '00f1fc230ac99f9b97ba1a7c214eb5b909a78660cb3826fca7d64c3af5a14848'),
    'times.ttf': (330412, '4e98adeff8ccc8ef4e3ece8d4547e288ff85fdc9c7ca711a4599c234874bbe86'),
    'timesbd.ttf': (333900, '4357b63cef20c01661a53c5dae70ffd20cb4765503aaed6d38b17a57c5a90bff'),
    'timesbi.ttf': (238612, '192e1b0d18e90334e999a99f8c32808d6a2e74b3698b8cd90c943c2249a46549'),
    'timesi.ttf': (247092, 'c25ae529b4cecdbca148b6ccb862ee0abad770af8b1fd29c8dba619d1b8da78a'),
    'trebuc.ttf': (126796, 'ec3ffb302488251e1b67fb09dd578b364c5339e27c1cfb26eb627666236453d0'),
    'trebucbd.ttf': (123828, 'f65941f9487c0a0a3b7445996ecbbd24466d7ae76ea2a597ced55f438fa63838'),
    'trebucbi.ttf': (131188, 'c0a6bdf31f9f2953b2f08a0c1734c892bc825f0fb17c604d420f7acf203a213b'),
    'trebucit.ttf': (139288, 'db56fdac7d3ba20b7aededcb6ee86c46687489d17b759e1708ea4e2d21e38410'),
    'verdana.ttf': (139640, '96ed14949ca4b7392cff235b9c41d55c125382abbe0c0d3c2b9dd66897cae0cb'),
    'verdanab.ttf': (136032, 'c8f5065ba91680f596af3b0378e2c3e713b95a523be3d56ae185ca2b8f5f0b23'),
    'verdanai.ttf': (154264, '91b59186656f52972531a11433c866fd56e62ec4e61e2621a2dba70c8f19a828'),
    'verdanaz.ttf': (153324, '698e220f48f4a40e77af7eb34958c8fd02f1e18c3ba3f365d93bfa2ed4474c80'),
    'webdings.ttf': (118752, '10d099c88521b1b9e380b7690cbe47b54bb19396ca515358cfdc15ac249e2f5d'),
}


def corefonts_artifact_paths(machine):
    fonts_dir = machine / 'drive_c/windows/Fonts'
    for name, (size, digest) in COREFONTS_ARTIFACTS.items():
        yield fonts_dir / name, size, digest


def verify_corefonts_artifacts(machine):
    """Read only, through descriptors (a link, a FIFO or a device in a name is refused, never followed or waited on)."""
    fonts_dir = machine / 'drive_c/windows/Fonts'
    try:
        fonts = machine_open_for_reading(machine, ('drive_c', 'windows', 'Fonts'))
    except ValueError:
        raise ValueError('Invalid fonts directory: ' + str(fonts_dir)) from None
    try:
        names = os.listdir(fonts)
        for path, expected_size, digest in corefonts_artifact_paths(machine):
            name = path.name
            if name not in names:
                name = next((n for n in names if n.casefold() == path.name.casefold()), name)
            actual_path = fonts_dir / name
            try:
                info = os.stat(name, dir_fd=fonts, follow_symlinks=False)
            except FileNotFoundError:
                info = None
            if info is not None and stat.S_ISLNK(info.st_mode):
                raise ValueError('Core fonts artifact is a symlink: ' + str(actual_path))
            if info is None or not stat.S_ISREG(info.st_mode):
                raise ValueError('Core fonts artifact missing: ' + str(actual_path))
            if info.st_size != expected_size:
                raise ValueError(f'Core fonts artifact size mismatch for {name}: expected {expected_size}, got {info.st_size}')
            if info.st_mode & 0o444 != 0o444:
                raise ValueError('Core fonts artifact not readable by all users: ' + str(actual_path))
            try:
                fd = machine_open_regular(fonts, [name])
                try:
                    opened = os.fstat(fd)
                    if (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino):
                        raise ValueError('replaced')
                    actual = machine_fd_sha256(fd, expected_size)
                finally:
                    os.close(fd)
            except ValueError:
                actual = None
            if actual != digest:
                raise ValueError('Approved core fonts artifact changed: ' + str(actual_path))
    finally:
        os.close(fonts)


def corefonts_fonts_fd(machine):
    """windows/Fonts of the machine root as a checked descriptor (machine_open_path), or None when it is missing."""
    try:
        return machine_open_path(machine, ('drive_c', 'windows', 'Fonts'), machine_admin_gid(machine))
    except FileNotFoundError:
        return None


def corefonts_drop_links(machine):
    """Before the installer runs: links in the names of the core fonts (any case) are removed, by descriptor."""
    fonts = corefonts_fonts_fd(machine)
    if fonts is None:
        return
    try:
        wanted = {name.casefold() for name in COREFONTS_ARTIFACTS}
        for entry in os.listdir(fonts):
            if entry.casefold() not in wanted:
                continue
            try:
                if stat.S_ISLNK(os.stat(entry, dir_fd=fonts, follow_symlinks=False).st_mode):
                    os.unlink(entry, dir_fd=fonts)
            except FileNotFoundError:
                pass
    finally:
        os.close(fonts)


def corefonts_publish_modes(machine):
    """After the installer: the core fonts (any case) get the Fonts directory's group and 644, through a descriptor of
    each regular file (a link, a FIFO or a file with other names is left alone)."""
    fonts = corefonts_fonts_fd(machine)
    if fonts is None:
        return
    try:
        group = os.fstat(fonts).st_gid
        wanted = {name.casefold() for name in COREFONTS_ARTIFACTS}
        for entry in os.listdir(fonts):
            if entry.casefold() not in wanted:
                continue
            try:
                fd = machine_open_regular(fonts, [entry])
            except ValueError:
                continue
            try:
                if os.fstat(fd).st_nlink == 1:          # a hard link to a file elsewhere is never changed
                    os.fchown(fd, -1, group)
                    os.fchmod(fd, 0o644)
            except OSError:
                pass
            finally:
                os.close(fd)
    finally:
        os.close(fonts)


def repair_runtime_artifacts(machine, cache):
    """Publish only verified minimum-runtime DLLs, never through prefix links.

    The vendor MSI may retain higher-version Wine builtins. cabextract can also
    return zero for a missing -F match, so its exit status alone proves nothing.
    Stage and validate both architectures before changing any destination.
    """
    with tempfile.TemporaryDirectory(prefix='runtime-members-', dir=cache) as scratch:
        staged = []
        for payload in COMPONENTS_METADATA['vcrun2022']['payloads']:
            arch = payload['architecture']
            package = cache / 'vcrun2022' / payload['name']
            if package.is_symlink() or hashlib.sha256(package.read_bytes()).hexdigest() != payload['sha256']:
                raise ValueError('Runtime package changed before extraction')
            cabinet = Path(scratch) / (arch + '.cab')
            with cabinet.open('wb') as out:
                subprocess.run(['/usr/bin/cabextract', '-p', '-F', 'a10' if arch == 'x86' else 'a12',
                                str(package)], stdout=out, check=True, timeout=30)
            directory = 'syswow64' if arch == 'x86' else 'system32'
            for name, digest in RUNTIME_ARTIFACTS[arch].items():
                source = Path(scratch) / (arch + '-' + name + '.dll')
                member = name + '.dll_' + ('x86' if arch == 'x86' else 'amd64')
                with source.open('wb') as out:
                    subprocess.run(['/usr/bin/cabextract', '-p', '-F', member, str(cabinet)],
                                   stdout=out, check=True, timeout=30)
                data = source.read_bytes()
                if hashlib.sha256(data).hexdigest() != digest:
                    raise ValueError('Runtime member hash mismatch or missing member: ' + member)
                pe = struct.unpack_from('<I', data, 0x3c)[0] if len(data) >= 64 else len(data)
                expected_machine = 0x14c if arch == 'x86' else 0x8664
                if (data[:2] != b'MZ' or pe + 6 > len(data) or data[pe:pe + 4] != b'PE\0\0' or
                        struct.unpack_from('<H', data, pe + 4)[0] != expected_machine):
                    raise ValueError('Wrong runtime member architecture: ' + member)
                staged.append((directory, name + '.dll', data))
        publish_system_files(machine, staged)
        verify_runtime_artifacts(machine)


def publish_system_files(machine, staged):
    """Publish (directory, name, bytes) items into windows/system32 or syswow64 of the machine root: every destination
    directory is opened and checked first (by descriptor, never through a link), then each file is written under a
    temporary name and renamed over its name, root and the machine group, 775 like every library there."""
    gid = machine_admin_gid(machine)
    directories = {}
    try:
        for directory, _, _ in staged:
            if directory not in directories:
                directories[directory] = machine_open_path(machine, ('drive_c', 'windows', directory), gid)
        for directory, name, data in staged:
            machine_write_file(directories[directory], name, data, gid, 0o775)
    finally:
        for descriptor in directories.values():
            os.close(descriptor)


def approved_runtime_recipe(path=None):
    """Only two exact download statements may differ from Debian's pinned script.

    The reviewed vcrun2022 function has no w_call dependencies. Its two payloads
    are preverified; execution is network-isolated so implicit downloads cannot
    expand the approved package set. Preserve upstream license notices in full.
    """
    winetricks = path or WINETRICKS_PATH
    validate_winetricks_integrity(winetricks)
    content = winetricks.read_text(encoding='utf-8')
    old = {'x86': 'dd1a8be03398367745a87a5e35bebdab00fdad080cf42af0c3f20802d08c25d4',
           'x64': '1821577409c35b2b9505ac833e246376cc68a8262972100444010b57226f0940'}
    payloads = COMPONENTS_METADATA['vcrun2022']['payloads']
    if {p['architecture'] for p in payloads} != {'x86', 'x64'} or len(payloads) != 2:
        raise ValueError('Incomplete runtime architecture manifest')
    for payload in payloads:
        arch = payload['architecture']
        original = 'w_download https://aka.ms/vs/17/release/vc_redist.' + arch + '.exe ' + old[arch]
        replacement = 'w_download ' + payload['url'] + ' ' + payload['sha256'] + ' ' + payload['name']
        if content.count(original) != 1:
            raise ValueError('Reviewed runtime recipe changed')
        content = content.replace(original, replacement)
    return content.encode('utf-8')


def cache_runtime_payloads(cache):
    """Called only after install() has rechecked explicit terms acceptance."""
    destination = cache / 'vcrun2022'
    if destination.is_symlink():
        raise ValueError('Refusing symlinked runtime payload cache')
    destination.mkdir(mode=0o700, exist_ok=True)
    if destination.stat().st_uid != 0 or destination.stat().st_mode & 0o022:
        raise ValueError('Runtime cache must be root-owned and private')
    for payload in COMPONENTS_METADATA['vcrun2022']['payloads']:
        target = destination / payload['name']
        if target.exists() or target.is_symlink():
            if (target.is_symlink() or not target.is_file() or target.stat().st_uid != 0 or
                    target.stat().st_mode & 0o022):
                raise ValueError('Untrusted cached runtime payload')
            if target.stat().st_size == payload['size'] and hashlib.sha256(target.read_bytes()).hexdigest() == payload['sha256']:
                continue
            raise ValueError('Corrupt runtime cache; no installer started')
        temporary = None
        try:
            with urllib.request.urlopen(payload['url'], timeout=20) as response:
                if not response.url.startswith('https://download.visualstudio.microsoft.com/'):
                    raise ValueError('Unexpected runtime download redirect')
                with tempfile.NamedTemporaryFile(dir=destination, prefix='.download-', delete=False) as out:
                    temporary = Path(out.name)
                    total, digest = 0, hashlib.sha256()
                    deadline = time.monotonic() + 300
                    while True:
                        chunk = response.read(65536)
                        if not chunk:
                            break
                        total += len(chunk)
                        if total > payload['size'] or time.monotonic() > deadline:
                            raise ValueError('Runtime download exceeded approved size/deadline')
                        digest.update(chunk)
                        out.write(chunk)
                    out.flush()
                    os.fsync(out.fileno())
            if total != payload['size'] or digest.hexdigest() != payload['sha256']:
                raise ValueError('Runtime payload hash/size mismatch; no installer started')
            os.replace(temporary, target)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)


def cache_webview2_payload(cache):
    """Download Microsoft's pinned standalone installer. Called only after install()
    has rechecked explicit terms acceptance; the file is verified before it runs."""
    payload = COMPONENTS_METADATA['webview2']['payloads'][0]
    destination = cache / 'webview2'
    if destination.is_symlink():
        raise ValueError('Refusing symlinked WebView2 payload cache')
    destination.mkdir(mode=0o700, exist_ok=True)
    if destination.stat().st_uid != 0 or destination.stat().st_mode & 0o022:
        raise ValueError('WebView2 cache must be root-owned and private')
    target = destination / payload['name']
    if target.exists() or target.is_symlink():
        if (target.is_symlink() or not target.is_file() or target.stat().st_uid != 0 or
                target.stat().st_mode & 0o022):
            raise ValueError('Untrusted cached WebView2 payload')
        if target.stat().st_size == payload['size'] and hashlib.sha256(target.read_bytes()).hexdigest() == payload['sha256']:
            return target
        target.unlink()
    temporary = None
    try:
        with urllib.request.urlopen(payload['url'], timeout=30) as response:
            if urllib.parse.urlsplit(response.url).hostname != payload['host']:
                raise ValueError('Unexpected WebView2 download redirect')
            with tempfile.NamedTemporaryFile(dir=destination, prefix='.download-', delete=False) as out:
                temporary = Path(out.name)
                total, digest = 0, hashlib.sha256()
                deadline = time.monotonic() + 1200
                while True:
                    chunk = response.read(1 << 20)
                    if not chunk:
                        break
                    total += len(chunk)
                    if total > payload['size'] or time.monotonic() > deadline:
                        raise ValueError('WebView2 download exceeded approved size/deadline')
                    digest.update(chunk)
                    out.write(chunk)
                out.flush()
                os.fsync(out.fileno())
        if total != payload['size'] or digest.hexdigest() != payload['sha256']:
            raise ValueError('WebView2 installer hash/size mismatch (Microsoft may have replaced the file); nothing was run')
        os.chmod(temporary, 0o644)
        os.replace(temporary, target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return target


# The installer also registers Microsoft's update tasks and COM classes. These
# EdgeUpdate policies turn automatic updates off (0 = updates disabled), so the
# runtime stays on the pinned, verified version.
WEBVIEW2_POLICY_KEY = r'HKLM\SOFTWARE\Policies\Microsoft\EdgeUpdate'
WEBVIEW2_POLICY = (('UpdateDefault', 0), ('Update{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 0),
                   ('AutoUpdateCheckPeriodMinutes', 0))
# The apps of Microsoft's updater (Edge Update) installed by their own installers; WebView2 first (COMPONENTS order).
EDGEUPDATE_APPS = ('webview2', 'edge', 'copilot')
EDGEUPDATE_MSI_POLICY = 'edgeupdate-msi-machine-v1'


def edgeupdate_policy(verb):
    """The EdgeUpdate policies an app's install sets: updates off, for that app as well."""
    if verb == 'webview2':
        return WEBVIEW2_POLICY
    return (('UpdateDefault', 0), ('Update' + COMPONENTS_METADATA[verb]['client_guid'], 0),
            ('AutoUpdateCheckPeriodMinutes', 0))


def msi_dos_path(machine, path):
    """PATH as msiexec of the machine prefix sees it: its Z: drive (msiexec would read a Unix path as an option)."""
    try:
        if os.readlink(Path(machine) / 'dosdevices' / 'z:') != '/':
            raise OSError
    except OSError:
        raise ValueError('The machine prefix has no Z: drive for the root directory') from None
    return 'Z:' + str(path).replace('/', '\\')


def registry_section(lines, header):
    """The value lines of one key of a Wine .reg hive (up to the next key's header), or None.

    header is the whole bracketed key name, so a subkey (same prefix, longer name) never matches."""
    header = header.lower()
    for index, line in enumerate(lines):
        if line.lower().startswith(header):
            section = []
            for entry in lines[index + 1:]:
                if entry.startswith('['):
                    break
                section.append(entry)
            return section
    return None


def verify_webview2_artifacts(machine, verb='webview2'):
    """The program folder and the EdgeUpdate registration the installer of VERB (an EDGEUPDATE_APPS one) must leave."""
    meta = COMPONENTS_METADATA[verb]
    version = meta['release']
    title = 'WebView2' if verb == 'webview2' else meta['title']
    label = 'WebView2 runtime' if verb == 'webview2' else title
    base = machine / 'drive_c/Program Files (x86)/Microsoft'
    artifacts = meta.get('artifacts', ['EdgeWebView/Application/{release}/msedgewebview2.exe',
                                       'EdgeWebView/Application/{release}/msedge.dll'])
    # Read only, through descriptors (a link, a FIFO or a device in a name is refused, never followed or waited on).
    root = machine_open_for_reading(machine)
    try:
        for name in artifacts:
            path = base / name.format(release=version)
            try:
                fd = machine_open_regular(root, list(path.relative_to(machine).parts))
                try:
                    size = os.fstat(fd).st_size
                finally:
                    os.close(fd)
            except ValueError:
                size = 0
            if size < 1024 * 1024:
                raise ValueError(label + ' file missing: ' + str(path))
        try:
            raw = machine_read_regular(root, ['system.reg'], MAX_MACHINE_HIVE_BYTES)
        except ValueError:
            raise ValueError('Machine registry missing') from None
    finally:
        os.close(root)
    text = raw.decode('utf-8', errors='replace').replace('\r\n', '\n').replace('\r', '\n')   # as read_text() reads it
    key = (r'[Software\\Wow6432Node\\Microsoft\\EdgeUpdate\\Clients\\' + meta['client_guid'] + ']').lower()
    lines = text.split('\n')
    section = registry_section(lines, key)
    if section is None or ('"pv"="' + version + '"') not in section:
        raise ValueError(title + ' is not registered in the machine registry')
    # Wine shares HKLM\SOFTWARE\Policies between the 32- and 64-bit views.
    for hive in (r'[Software\\Policies\\Microsoft\\EdgeUpdate]',):
        block = registry_section(lines, hive)
        if block is None:
            raise ValueError(title + ' update policy missing: ' + hive)
        for name, data in edgeupdate_policy(verb):
            if f'"{name}"=dword:{data:08x}' not in block:
                raise ValueError(f'{title} update policy {name} not set in {hive}')


def cache_corefonts_payloads(cache, review_cache=None):
    """Stage verified Core Fonts packages from the review cache into winetricks cache.

    Called only after install() has rechecked explicit terms acceptance.
    """
    review_dir = (review_cache or REVIEW_CACHE) / 'corefonts'
    destination = cache / 'corefonts'
    if destination.is_symlink():
        raise ValueError('Refusing symlinked corefonts payload cache')
    destination.mkdir(mode=0o700, parents=True, exist_ok=True)
    if destination.stat().st_uid != 0 or destination.stat().st_mode & 0o022:
        raise ValueError('Core fonts cache must be root-owned and private')

    packages = review_packages('corefonts')
    for package in packages:
        name = package['name']
        size = package['size']
        sha256 = package['sha256']
        target = destination / name
        if target.exists() or target.is_symlink():
            if target.is_symlink() or not target.is_file() or target.stat().st_uid != 0 or target.stat().st_mode & 0o022:
                raise ValueError('Untrusted cached corefonts payload: ' + name)
            if target.stat().st_size == size and hashlib.sha256(target.read_bytes()).hexdigest() == sha256:
                continue
            raise ValueError('Corrupt corefonts cache; no installer started')
        source = review_dir / name
        if source.is_symlink() or not source.is_file() or source.stat().st_size != size:
            raise ReviewRequired(f'Core fonts payload {name} missing or changed in review cache')
        if hashlib.sha256(source.read_bytes()).hexdigest() != sha256:
            raise ValueError(f'Core fonts payload {name} integrity mismatch in review cache')
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=destination, prefix='.corefonts-', delete=False) as out:
                temporary = Path(out.name)
                os.fchmod(out.fileno(), 0o600)
                shutil.copyfile(source, temporary)
                out.flush()
                os.fsync(out.fileno())
            os.replace(temporary, target)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)


D3DCOMPILER_47_ARTIFACTS = {
    'x86': {
        'path': 'syswow64/d3dcompiler_47.dll',
        'member': 'fila319f706acfa16d6707473ebf29bdc7f',
        'cab': '2630bae9681db6a9f6722366f47d055c.cab',
        'size': 3466856,
        'sha256': 'efbdbbcd0d954f8fdc53467de5d89ad525e4e4a9cfff8a15d07c6fdb350c407f',
        'machine': 0x14c,
    },
    'x64': {
        'path': 'system32/d3dcompiler_47.dll',
        'member': 'fil3585cb2ea5db13cc0838f8d06b5c9679',
        'cab': '61d57a7a82309cd161a854a6f4619e52.cab',
        'size': 4173928,
        'sha256': 'e994847e01a6f1e4cbdc5a864616ac262f67ee4f14db194984661a8d927ab7f4',
        'machine': 0x8664,
    },
}


def d3dcompiler_artifact_paths(machine):
    for arch in ('x86', 'x64'):
        entry = D3DCOMPILER_47_ARTIFACTS[arch]
        yield machine / 'drive_c/windows' / entry['path'], entry


def verify_d3dcompiler_artifacts(machine):
    """Read only, through descriptors (a link, a FIFO or a device in a name is refused, never followed or waited on)."""
    try:
        root = machine_open_for_reading(machine, ('drive_c', 'windows'))
    except ValueError:
        raise ValueError('d3dcompiler_47 artifact missing: ' + str(machine / 'drive_c/windows')) from None
    try:
        for target, entry in d3dcompiler_artifact_paths(machine):
            directory, name = entry['path'].split('/')
            try:
                parent = os.open(directory, DIR_OPEN_FLAGS, dir_fd=root)
            except OSError:
                raise ValueError('d3dcompiler_47 artifact missing: ' + str(target)) from None
            try:
                try:
                    info = os.stat(name, dir_fd=parent, follow_symlinks=False)
                except FileNotFoundError:
                    info = None
                if info is not None and stat.S_ISLNK(info.st_mode):
                    raise ValueError('d3dcompiler_47 artifact is a symlink: ' + str(target))
                if info is None or not stat.S_ISREG(info.st_mode):
                    raise ValueError('d3dcompiler_47 artifact missing: ' + str(target))
                if info.st_size != entry['size']:
                    raise ValueError(f'd3dcompiler_47 artifact size mismatch for {target.name}: expected {entry["size"]}, got {info.st_size}')
                if info.st_mode & 0o444 != 0o444:
                    raise ValueError('d3dcompiler_47 artifact not readable by all users: ' + str(target))
                try:
                    data = machine_read_regular(parent, [name], entry['size'])
                except ValueError:
                    raise ValueError('d3dcompiler_47 artifact missing: ' + str(target)) from None
            finally:
                os.close(parent)
            if len(data) != entry['size'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
                raise ValueError('d3dcompiler_47 artifact hash mismatch: ' + str(target))
            if not pe_machine_is(data, entry['machine']):
                raise ValueError('Wrong d3dcompiler_47 artifact architecture: ' + str(target))
    finally:
        os.close(root)


def repair_d3dcompiler_artifacts(machine, cache):
    """Publish only verified d3dcompiler_47 DLLs, never through prefix links."""
    with tempfile.TemporaryDirectory(prefix='d3dcompiler-members-', dir=cache) as scratch:
        staged = []
        for arch in ('x86', 'x64'):
            entry = D3DCOMPILER_47_ARTIFACTS[arch]
            cabinet = cache / 'd3dcompiler_47' / entry['cab']
            if cabinet.is_symlink() or not cabinet.is_file():
                raise ValueError('d3dcompiler_47 cabinet missing before extraction: ' + entry['cab'])
            expected_cab_hash = next(sha for name, sha in COMPONENTS_METADATA['d3dcompiler_47']['recipe_files'] if name == entry['cab'])
            if hashlib.sha256(cabinet.read_bytes()).hexdigest() != expected_cab_hash:
                raise ValueError('d3dcompiler_47 cabinet changed before extraction: ' + entry['cab'])
            source = Path(scratch) / (arch + '-d3dcompiler_47.dll')
            with source.open('wb') as out:
                subprocess.run(['/usr/bin/cabextract', '-p', '-F', entry['member'], str(cabinet)],
                               stdout=out, check=True, timeout=30)
            data = source.read_bytes()
            if len(data) != entry['size'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
                raise ValueError('d3dcompiler_47 member hash mismatch or missing member: ' + entry['member'])
            pe = struct.unpack_from('<I', data, 0x3c)[0] if len(data) >= 64 else len(data)
            if (data[:2] != b'MZ' or pe + 6 > len(data) or data[pe:pe + 4] != b'PE\0\0' or
                    struct.unpack_from('<H', data, pe + 4)[0] != entry['machine']):
                raise ValueError('Wrong d3dcompiler_47 member architecture: ' + entry['member'])
            directory, name = entry['path'].split('/')
            staged.append((directory, name, data))
        publish_system_files(machine, staged)
        verify_d3dcompiler_artifacts(machine)


def approved_d3dcompiler_recipe(path=None):
    """Replace mozilla GitHub downloads in load_d3dcompiler_47 with first-party Microsoft SDK payloads."""
    winetricks = path or WINETRICKS_PATH
    validate_winetricks_integrity(winetricks)
    content = winetricks.read_text(encoding='utf-8')
    orig_32 = ('w_download https://raw.githubusercontent.com/mozilla/fxc2/master/dll/d3dcompiler_47_32.dll '
               '2ad0d4987fc4624566b190e747c9d95038443956ed816abfd1e2d389b5ec0851')
    repl_32 = ('w_download https://download.microsoft.com/download/B/0/C/B0C80BA3-8AD6-4958-810B-6882485230B5/standalonesdk/Installers/2630bae9681db6a9f6722366f47d055c.cab '
               'efbdbbcd0d954f8fdc53467de5d89ad525e4e4a9cfff8a15d07c6fdb350c407f d3dcompiler_47_32.dll')
    orig_64 = ('w_download https://raw.githubusercontent.com/mozilla/fxc2/master/dll/d3dcompiler_47.dll '
               '4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3')
    repl_64 = ('w_download https://download.microsoft.com/download/B/0/C/B0C80BA3-8AD6-4958-810B-6882485230B5/standalonesdk/Installers/61d57a7a82309cd161a854a6f4619e52.cab '
               'e994847e01a6f1e4cbdc5a864616ac262f67ee4f14db194984661a8d927ab7f4 d3dcompiler_47.dll')
    if content.count(orig_32) != 1 or content.count(orig_64) != 1:
        raise ValueError('Reviewed d3dcompiler_47 recipe changed')
    content = content.replace(orig_32, repl_32).replace(orig_64, repl_64)
    return content.encode('utf-8')


def cache_d3dcompiler_payloads(cache, review_cache=None):
    """Stage verified d3dcompiler_47 packages and pre-extracted DLLs into winetricks cache.

    Called only after install() has rechecked explicit terms acceptance.
    """
    review_dir = (review_cache or REVIEW_CACHE) / 'd3dcompiler_47'
    destination = cache / 'd3dcompiler_47'
    if destination.is_symlink():
        raise ValueError('Refusing symlinked d3dcompiler payload cache')
    destination.mkdir(mode=0o700, parents=True, exist_ok=True)
    if destination.stat().st_uid != 0 or destination.stat().st_mode & 0o022:
        raise ValueError('d3dcompiler cache must be root-owned and private')

    packages = review_packages('d3dcompiler_47')
    for package in packages:
        name = package['name']
        size = package['size']
        sha256 = package['sha256']
        target = destination / name
        if target.exists() or target.is_symlink():
            if target.is_symlink() or not target.is_file() or target.stat().st_uid != 0 or target.stat().st_mode & 0o022:
                raise ValueError('Untrusted cached d3dcompiler payload: ' + name)
            if target.stat().st_size == size and hashlib.sha256(target.read_bytes()).hexdigest() == sha256:
                continue
            raise ValueError('Corrupt d3dcompiler cache; no installer started')
        source = review_dir / name
        if source.is_symlink() or not source.is_file() or source.stat().st_size != size:
            raise ReviewRequired(f'd3dcompiler payload {name} missing or changed in review cache')
        if hashlib.sha256(source.read_bytes()).hexdigest() != sha256:
            raise ValueError(f'd3dcompiler payload {name} integrity mismatch in review cache')
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=destination, prefix='.d3dcompiler-', delete=False) as out:
                temporary = Path(out.name)
                os.fchmod(out.fileno(), 0o600)
                shutil.copyfile(source, temporary)
                out.flush()
                os.fsync(out.fileno())
            os.replace(temporary, target)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)

    dll_targets = [
        ('d3dcompiler_47_32.dll', D3DCOMPILER_47_ARTIFACTS['x86']),
        ('d3dcompiler_47.dll', D3DCOMPILER_47_ARTIFACTS['x64']),
    ]
    for dll_name, entry in dll_targets:
        dll_path = destination / dll_name
        if dll_path.is_file() and not dll_path.is_symlink():
            if dll_path.stat().st_size == entry['size'] and hashlib.sha256(dll_path.read_bytes()).hexdigest() == entry['sha256']:
                continue
        cabinet = destination / entry['cab']
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=destination, prefix='.extracted-', delete=False) as out:
                temporary = Path(out.name)
                subprocess.run(['/usr/bin/cabextract', '-p', '-F', entry['member'], str(cabinet)],
                               stdout=out, check=True, timeout=30)
                out.flush()
                os.fsync(out.fileno())
            data = temporary.read_bytes()
            if len(data) != entry['size'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
                raise ValueError(f'Extracted {dll_name} failed verification')
            os.replace(temporary, dll_path)
            os.chmod(dll_path, 0o600)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)


IS_ADMIN_HELPER = '/usr/local/libexec/wineexplorer/wex-is-admin'


def is_admin():
    if os.getuid() == 0:
        return True
    try:
        user = pwd.getpwuid(os.getuid()).pw_name
        return subprocess.run([IS_ADMIN_HELPER, user], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, check=False).returncode == 0
    except (OSError, KeyError):
        return False


def protected_name(name):
    name = name.lower()
    return name.endswith('.dll') and (name[:-4] in PROTECTED or name.startswith(('msxml', 'mf')))


MAX_TERMS_BYTES = 2 * 1024 * 1024


def component_identity(verb):
    record = {'schema': 1, 'verb': validate_verb(verb),
              'metadata': COMPONENTS_METADATA[verb],
              'winetricks_sha256': APPROVED_WINETRICKS_SHA256,
              'architecture': 'wow64'}
    if verb == 'vcrun2022':
        record['runtime_artifacts'] = RUNTIME_ARTIFACTS
    elif verb == 'corefonts':
        record['corefonts_artifacts'] = COREFONTS_ARTIFACTS
    elif verb == 'd3dcompiler_47':
        record['d3dcompiler_artifacts'] = D3DCOMPILER_47_ARTIFACTS
    elif verb == 'webview2':
        record['webview2_policy'] = [WEBVIEW2_POLICY_KEY, WEBVIEW2_POLICY]
    elif verb in EDGEUPDATE_APPS:
        record['edgeupdate_policy'] = [WEBVIEW2_POLICY_KEY, edgeupdate_policy(verb)]
    return hashlib.sha256(json.dumps(record, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def atomic_json(path, data, mode=0o600):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix='.' + path.name, delete=False) as out:
            temporary = Path(out.name)
            os.fchmod(out.fileno(), mode)
            json.dump(data, out, sort_keys=True, indent=2)
            out.write('\n')
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def extract_docx_terms(data):
    """Render supported Word paragraphs/lists/links; reject unhandled content.

    Only independently hash-pinned documents reach this parser in production.
    No proprietary document contents are embedded in the program.
    """
    w = '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}'
    rel = '{http://schemas.openxmlformats.org/officeDocument/2006/relationships}'
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = archive.namelist()
        if len(set(names)) != len(names) or sum(i.file_size for i in archive.infolist()) > MAX_TERMS_BYTES:
            raise ValueError('Oversized or ambiguous terms document')
        document = ET.fromstring(archive.read('word/document.xml'))
        if any(document.find('.//' + w + tag) is not None for tag in
               ('altChunk', 'object', 'drawing', 'del', 'fldSimple', 'instrText', 'footnoteReference', 'endnoteReference')):
            raise ValueError('Terms contain unsupported embedded or conditional content')
        # Do not silently discard substantive text in auxiliary document parts.
        for name in names:
            if re.fullmatch(r'word/(?:header\d+|footer\d+|footnotes|endnotes)\.xml', name):
                part = ET.fromstring(archive.read(name))
                if any((node.text or '').strip() for node in part.iter(w + 't')):
                    raise ValueError('Terms contain auxiliary text requiring explicit review')
        links = {}
        if 'word/_rels/document.xml.rels' in names:
            for item in ET.fromstring(archive.read('word/_rels/document.xml.rels')):
                if item.get('TargetMode') == 'External':
                    if not item.get('Type', '').endswith('/hyperlink'):
                        raise ValueError('Unsupported external terms dependency')
                    url = item.get('Target', '')
                    if not url.startswith(('https://', 'http://')):
                        raise ValueError('Unsupported terms hyperlink')
                    links[item.get('Id')] = url
        numbering = ET.fromstring(archive.read('word/numbering.xml')) if 'word/numbering.xml' in names else None
        abstracts, numbers, counters = {}, {}, {}
        if numbering is not None:
            abstracts = {n.get(w + 'abstractNumId'): n for n in numbering.findall(w + 'abstractNum')}
            for number in numbering.findall(w + 'num'):
                if number.find(w + 'lvlOverride') is not None:
                    raise ValueError('Numbering overrides require explicit review')
                abstract = number.find(w + 'abstractNumId')
                if abstract is not None:
                    numbers[number.get(w + 'numId')] = abstract.get(w + 'val')
        paragraphs = []
        styles = {}
        if 'word/styles.xml' in names:
            styles = {s.get(w + 'styleId'): s for s in
                      ET.fromstring(archive.read('word/styles.xml')).findall(w + 'style')}

        def list_properties(properties, seen=()):
            result = {}
            if properties is None:
                return result
            style = properties.find(w + 'pStyle')
            if style is not None:
                style_id = style.get(w + 'val')
                if style_id in seen or len(seen) > 16:
                    raise ValueError('Cyclic legal document style')
                definition = styles.get(style_id)
                if definition is not None:
                    base = definition.find(w + 'basedOn')
                    if base is not None:
                        inherited = ET.Element(w + 'pPr')
                        ET.SubElement(inherited, w + 'pStyle', {w + 'val': base.get(w + 'val', '')})
                        result.update(list_properties(inherited, (*seen, style_id)))
                    result.update(list_properties(definition.find(w + 'pPr'), (*seen, style_id)))
            num = properties.find(w + 'numPr')
            if num is not None:
                for field in ('numId', 'ilvl'):
                    item = num.find(w + field)
                    if item is not None:
                        result[field] = item.get(w + 'val')
            return result

        def format_counter(value, fmt):
            if value < 1 or value > 3999:
                raise ValueError('Unsupported legal numbering range')
            if fmt == 'decimal':
                return str(value)
            if fmt in ('lowerLetter', 'upperLetter'):
                marker = ''
                while value:
                    value, digit = divmod(value - 1, 26)
                    marker = chr(ord('a') + digit) + marker
                return marker.upper() if fmt == 'upperLetter' else marker
            if fmt in ('lowerRoman', 'upperRoman'):
                marker = ''
                for amount, symbol in ((1000, 'M'), (900, 'CM'), (500, 'D'), (400, 'CD'),
                                       (100, 'C'), (90, 'XC'), (50, 'L'), (40, 'XL'),
                                       (10, 'X'), (9, 'IX'), (5, 'V'), (4, 'IV'), (1, 'I')):
                    count, value = divmod(value, amount)
                    marker += symbol * count
                return marker.lower() if fmt == 'lowerRoman' else marker
            raise ValueError('Unsupported legal list format')

        for paragraph in document.iter(w + 'p'):
            pieces = []
            for node in paragraph.iter():
                if node.tag == w + 't':
                    pieces.append(node.text or '')
                elif node.tag == w + 'tab':
                    pieces.append('\t')
                elif node.tag in (w + 'br', w + 'cr'):
                    pieces.append('\n')
                elif node.tag == w + 'hyperlink':
                    link_id = node.get(rel + 'id')
                    if link_id and link_id not in links:
                        raise ValueError('Unresolved document hyperlink')
            text = ''.join(pieces)
            for link in paragraph.iter(w + 'hyperlink'):
                url = links.get(link.get(rel + 'id'))
                if url and url not in text:
                    text += ' [' + url + ']'
            properties = paragraph.find(w + 'pPr')
            num = list_properties(properties)
            if num:
                num_id = num.get('numId', '0')
                if num_id != '0':
                    level_id = num.get('ilvl', '0')
                    depth = int(level_id)
                    abstract = abstracts.get(numbers.get(num_id))
                    level = next((l for l in abstract.findall(w + 'lvl') if l.get(w + 'ilvl') == level_id), None) if abstract is not None else None
                    if level is None or not 0 <= depth <= 8:
                        raise ValueError('Unsupported legal list nesting')
                    fmt, label = level.find(w + 'numFmt'), level.find(w + 'lvlText')
                    if fmt is None or label is None:
                        raise ValueError('Incomplete legal list numbering')
                    if fmt.get(w + 'val') == 'bullet':
                        prefix = '\u2022 '
                    else:
                        start = level.find(w + 'start')
                        key = (num_id, depth)
                        counters[key] = counters.get(key, int(start.get(w + 'val', '1')) - 1 if start is not None else 0) + 1
                        for deeper in range(depth + 1, 9):
                            counters.pop((num_id, deeper), None)
                        if level.find(w + 'lvlRestart') is not None:
                            raise ValueError('Custom list restart requires explicit review')
                        def replace_level(match):
                            referenced = int(match.group(1)) - 1
                            ref_level = next((l for l in abstract.findall(w + 'lvl') if l.get(w + 'ilvl') == str(referenced)), None)
                            if ref_level is None or (num_id, referenced) not in counters:
                                raise ValueError('Unresolved parent legal list counter')
                            ref_fmt = ref_level.find(w + 'numFmt')
                            return format_counter(counters[(num_id, referenced)], ref_fmt.get(w + 'val', ''))
                        prefix = re.sub(r'%([1-9])', replace_level, label.get(w + 'val', '')) + ' '
                    text = prefix + text
            if text.strip():
                paragraphs.append(text)
        text = '\n\n'.join(paragraphs) + '\n'
        if len(text.strip()) < 500 or len(text.encode('utf-8')) > MAX_TERMS_BYTES or '\x00' in text:
            raise ValueError('Incomplete or oversized extracted terms')
        return text


def extract_webview2_terms(data):
    """Render the terms Microsoft publishes for the WebView2 Runtime as plain text.

    The download page fetches them as JSON ({"evergreenHtml": "<div>...</div>"})
    and shows them in its licence dialog. Only text nodes are kept; nothing in
    the document is interpreted or fetched.
    """
    record = json.loads(data.decode('utf-8'))
    markup = record.get('evergreenHtml')
    if not isinstance(markup, str):
        raise ValueError('WebView2 terms document has no evergreenHtml')
    text = html_text(markup)
    if 'MICROSOFT EDGE WEBVIEW2 RUNTIME' not in text or 'FIXED VERSION' in text:
        raise ValueError('Terms document is not the WebView2 Evergreen Runtime agreement')
    return text


def html_text(markup, main_only=False):
    """The text nodes of MARKUP as paragraphs (one per block element); nothing in it is interpreted or fetched. With
    MAIN_ONLY the text of the page's <main> element only, without scripts and styles (a vendor's terms web page)."""
    import html.parser

    class Text(html.parser.HTMLParser):
        BLOCKS = {'p', 'div', 'h1', 'h2', 'h3', 'h4', 'li', 'br', 'hr'}
        SKIP = {'script', 'style', 'noscript', 'template', 'svg'} if main_only else set()

        def __init__(self):
            super().__init__(convert_charrefs=True)
            self.parts = ['']
            self.skip = 0
            self.main = 0 if main_only else 1

        def handle_starttag(self, tag, attrs):
            if main_only and tag == 'main':
                self.main += 1
            if tag in self.SKIP:
                self.skip += 1
            if tag in self.BLOCKS:
                self.parts.append('')

        def handle_endtag(self, tag):
            if main_only and tag == 'main':
                self.main = max(self.main - 1, 0)
            if tag in self.SKIP:
                self.skip = max(self.skip - 1, 0)
            if tag in self.BLOCKS:
                self.parts.append('')

        def handle_data(self, text):
            if self.main and not self.skip:
                self.parts[-1] += text

    parser = Text()
    parser.feed(markup)
    parser.close()
    paragraphs = [' '.join(part.split()) for part in parser.parts]
    text = '\n\n'.join(part for part in paragraphs if part) + '\n'
    if len(text.strip()) < 500 or len(text.encode('utf-8')) > MAX_TERMS_BYTES or '\x00' in text:
        raise ValueError('Incomplete or oversized extracted terms')
    return text


def extract_html_terms(data, start):
    """The terms on a vendor's web page: the text of its main element from the paragraph that begins with START."""
    text = html_text(data.decode('utf-8'), main_only=True)
    index = text.find('\n' + start)
    if not start or (index < 0 and not text.startswith(start)):
        raise ValueError('Terms page does not contain the reviewed agreement')
    text = text[index + 1:] if index >= 0 else text
    if len(text.strip()) < 500:
        raise ValueError('Incomplete extracted terms')
    return text


class StaleTermsError(ValueError):
    """Trusted cached document no longer matches the reviewed package policy."""


TERMS_DOCUMENT_HOSTS = ('https://visualstudio.microsoft.com/', 'https://raw.githubusercontent.com/Microsoft/',
                        'https://raw.githubusercontent.com/microsoft/', 'https://api.nuget.org/v3-flatcontainer/',
                        'https://github.com/zcomputerwiz/uno-uwp/releases/download/',
                        'https://raw.githubusercontent.com/PowerShell/PowerShell/',
                        'https://release-assets.githubusercontent.com/')


def terms_documents_identity(documents):
    """The pinned identity of a set of terms documents: the hash of their titles, locations and byte hashes."""
    return hashlib.sha256(json.dumps([[d['title'], d['url'], d.get('member', ''), d['format'], d['sha256']]
                                      for d in documents], separators=(',', ':')).encode()).hexdigest()


def _terms_download(url, headers=None, approved_hosts=None):
    """The document at URL; with APPROVED_HOSTS the final location after redirects must be one of them too."""
    req = urllib.request.Request(url, headers=dict({'User-Agent': 'Wget/1.21.3 (linux-gnu)'}, **(headers or {})))
    with urllib.request.urlopen(req, timeout=10) as response:
        if not response.url.startswith('https://'):
            raise ValueError('Insecure vendor document redirect')
        if approved_hosts is not None and not response.url.startswith(approved_hosts):
            raise ValueError('Vendor document redirected to an unapproved location')
        return response.read(MAX_TERMS_BYTES + 1), getattr(response, 'headers', None)


class _RangeReader(io.RawIOBase):
    """A remote file read with HTTP range requests (a licence inside a large package without downloading it)."""
    def __init__(self, url, size, approved_hosts=None):
        self.url, self.size, self.pos, self.requests, self.approved_hosts = url, size, 0, 0, approved_hosts
    def readable(self): return True
    def seekable(self): return True
    def tell(self): return self.pos
    def seek(self, offset, whence=0):
        self.pos = offset if whence == 0 else self.pos + offset if whence == 1 else self.size + offset
        return self.pos
    def readinto(self, buffer):
        if self.pos >= self.size or not len(buffer):
            return 0
        self.requests += 1
        if self.requests > 64:
            raise ValueError('Too many range requests for a terms document')
        end = min(self.pos + len(buffer), self.size) - 1
        data, headers = _terms_download(self.url, {'Range': f'bytes={self.pos}-{end}'}, self.approved_hosts)
        if len(data) != end - self.pos + 1:
            raise ValueError('Range request not honoured for a terms document')
        buffer[:len(data)] = data
        self.pos += len(data)
        return len(data)


def terms_document_text(verb, document, approved_hosts=None):
    """The text of one pinned terms document: {url, sha256, format: txt | docx | zip-member [, member, size]}."""
    url = document['url']
    if approved_hosts is not None and not url.startswith(approved_hosts):
        raise ValueError('Unapproved vendor document location')
    try:
        if document['format'] == 'zip-member':
            # a licence file inside a pinned package: only its central directory and the member are read
            with zipfile.ZipFile(io.BufferedReader(_RangeReader(url, document['size'], approved_hosts), 1 << 16)) as package:
                info = package.getinfo(document['member'])
                if info.file_size > MAX_TERMS_BYTES:
                    raise ValueError('Oversized vendor document')
                data = package.read(info)
        elif document['format'] == 'html':
            # Microsoft's web servers refuse Wget's name; this one is honest about what fetches the page
            data, _ = _terms_download(url, {'User-Agent': 'WineExplorer-components/1.0'}, approved_hosts)
        else:
            data, _ = _terms_download(url, approved_hosts=approved_hosts)
    except urllib.error.URLError as err:
        raise ConnectionError(f'Failed to retrieve vendor terms for {verb} from {url}: {err.reason}')
    if len(data) > MAX_TERMS_BYTES:
        raise ValueError('Vendor document exceeded its size limit; review required')
    if document['format'] == 'html':
        # the page's markup changes with every site build: the extracted agreement is what is pinned
        text = extract_html_terms(data, document.get('start', ''))
        if hashlib.sha256(text.encode()).hexdigest() != document['sha256']:
            raise ValueError('Vendor terms changed; review required')
        return text
    if hashlib.sha256(data).hexdigest() != document['sha256']:
        raise ValueError('Vendor document changed or exceeded its size limit; review required')
    if document['format'] == 'webview2-json':
        return extract_webview2_terms(data)
    if document['format'] == 'docx':
        return extract_docx_terms(data)
    if document['format'] in ('txt', 'zip-member'):
        # a plain UTF-8 licence document; its bytes are pinned above, the text of the whole set by the caller
        return data.decode('utf-8-sig', errors='strict')
    raise ValueError('Unreviewed vendor document format')


def fetch_vendor_terms(verb, directory=None):
    """Acquire authentic vendor terms over network and cache under directory."""
    directory = directory or TERMS
    validate_verb(verb)
    meta = COMPONENTS_METADATA.get(verb, {})
    if review_available(verb) and not meta.get('terms_url'):
        # Terms live inside the package: only the explicit review-download action
        # may fetch it. Opening/selecting/GET_TERMS never reaches the network.
        return embedded_license_text(verb, directory)
    if meta.get('disabled'):
        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')

    target = directory / (verb + '.json')
    if target.is_file():
        try:
            return license_text(verb, directory)
        except StaleTermsError:
            # A new explicit Review may refresh trusted but stale cached terms.
            # Insecure ownership/paths still fail above, never taking this path.
            pass

    if meta.get('terms_documents'):
        # several vendor documents, each pinned by its bytes, shown one after the other under their titles
        if not meta.get('terms_sha256') or meta['terms_sha256'] != terms_documents_identity(meta['terms_documents']):
            raise ValueError('No reviewed document set for ' + verb)
        text = '\n\n'.join(f'{document["title"]}\n{"=" * len(document["title"])}\n\n'
                           f'{terms_document_text(verb, document, TERMS_DOCUMENT_HOSTS).strip()}'
                           for document in meta['terms_documents']) + '\n'
    else:
        terms_url = meta.get('terms_url')
        if not terms_url:
            raise FileNotFoundError(f'Authentic vendor terms for {verb} must be provisioned in {directory}/{verb}.txt')
        if not terms_url.startswith('https://') or not meta.get('terms_sha256') or not meta.get('terms_text_sha256'):
            raise ValueError('No reviewed document and extraction identity for ' + verb)
        text = terms_document_text(verb, {'url': terms_url, 'sha256': meta['terms_sha256'],
                                          'start': meta.get('terms_start', ''),
                                          'format': meta['terms_format'] if meta.get('terms_format') in
                                          ('webview2-json', 'html') else
                                          'docx' if terms_url.endswith('.docx') else
                                          'txt' if terms_url.endswith('.txt') and
                                          meta.get('execution_policy') == UWP_XAML_POLICY else 'unreviewed'})
    if not meta.get('terms_text_sha256'):
        raise ValueError('No reviewed extraction identity for ' + verb)
    if hashlib.sha256(text.encode()).hexdigest() != meta['terms_text_sha256']:
        raise ValueError('Terms extraction changed; full document review required')

    if len(text.strip()) < 500:
        raise ValueError(f'Retrieved terms for {verb} too short ({len(text.strip())} chars); full vendor terms required')

    # If running as root, cache into directory securely
    if os.getuid() == 0:
        directory.mkdir(parents=True, exist_ok=True)
        for entry in (directory, *directory.parents):
            if entry.is_symlink() or entry.stat().st_uid != 0 or entry.stat().st_mode & 0o022:
                raise ValueError(f'Insecure vendor license path: {entry}')
        atomic_json(target, {'schema': 1, 'component_identity': component_identity(verb),
                             'document_sha256': meta['terms_sha256'], 'text': text}, 0o644)

    sha = hashlib.sha256((component_identity(verb) + '\n' + text).encode('utf-8')).hexdigest()
    return text, sha


def prepare_terms(verb, directory=None):
    """Privileged action: fetch and cache authentic vendor terms into root TERMS dir."""
    directory = directory or TERMS
    validate_verb(verb)
    if os.getuid() != 0:
        raise PermissionError('Preparing vendor terms requires root elevation')
    meta = COMPONENTS_METADATA.get(verb, {})
    if review_available(verb) and not meta.get('terms_url'):
        raise ValueError(f'{verb} terms are embedded in its package; use the explicit review download')
    if meta.get('disabled'):
        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
    return fetch_vendor_terms(verb, directory)


def license_text(verb, directory=None):
    directory = directory or TERMS
    meta = COMPONENTS_METADATA.get(validate_verb(verb), {})
    if review_available(verb) and not meta.get('terms_url'):
        return embedded_license_text(verb, directory)
    path = directory / (validate_verb(verb) + '.json')
    if not path.is_file():
        raise ValueError(f'{verb} terms have not been prepared. Choose Review terms; no payload was downloaded.')
    # No caller-controlled text or replaceable ancestor can authorize a root
    # download. The installed directory and notices must be root-owned.
    for entry in (path, *path.parents):
        if entry.is_symlink() or entry.stat().st_uid != 0 or entry.stat().st_mode & 0o022:
            raise ValueError(f'Insecure vendor license path: {entry}')
    if path.stat().st_size > MAX_TERMS_BYTES:
        raise ValueError('Oversized cached terms')
    record = json.loads(path.read_text(encoding='utf-8'))
    meta = COMPONENTS_METADATA[verb]
    text = record.get('text', '')
    if (record.get('schema') != 1 or record.get('component_identity') != component_identity(verb) or
            record.get('document_sha256') != meta.get('terms_sha256') or
            hashlib.sha256(text.encode()).hexdigest() != meta.get('terms_text_sha256')):
        raise StaleTermsError('Cached document/package identity changed; review required')
    if len(text.strip()) < 500 or '\x00' in text:
        raise ValueError(f'Full vendor terms required for {verb}')
    return text, hashlib.sha256((component_identity(verb) + '\n' + text).encode()).hexdigest()


# ---------------------------------------------------------------------------
# Embedded-terms review: explicit "Download package to review its license", static
# extraction, then a SEPARATE installation acceptance bound to the exact result.
#
# Nothing here executes package code, Wine, Winetricks or a vendor extractor, and
# nothing here reaches an install entry point, a receipt, the machine prefix or
# HKCU defaults. Permission to download is never recorded as acceptance: the only
# durable output is a verified private package cache plus the rendered terms.
# ---------------------------------------------------------------------------
REVIEW_CACHE = Path('/var/cache/wineexplorer/review')
REVIEW_RETENTION_SECONDS = 14 * 24 * 3600
REVIEW_DEADLINE_SECONDS = 1800
REVIEW_READ_TIMEOUT = 30
REVIEW_MAX_PACKAGE_BYTES = 128 * 1024 * 1024
REVIEW_TERMS_SCHEMA = 2
PE_MAX_SECTIONS = 96
PE_MAX_RESOURCE_ENTRIES = 512
PE_MAX_RESOURCE_BYTES = 64 * 1024
REVIEW_LOCK_WAIT_SECONDS = 60
REVIEW_SHUTDOWN_JOIN_SECONDS = 30
REVIEW_OPERATION_RETENTION = 16


class ReviewRequired(ValueError):
    """The package has not been downloaded for review (no network was used)."""


class ReviewCancelled(Exception):
    """The administrator cancelled a review download; nothing was accepted."""


def review_available(verb):
    return bool(COMPONENTS_METADATA.get(verb, {}).get('review'))


def review_meta(verb):
    meta = COMPONENTS_METADATA.get(validate_verb(verb), {})
    review = meta.get('review')
    if not review or review.get('mode') != 'embedded-package-terms':
        raise ValueError(f'{verb} has no reviewed embedded-terms route')
    return review


def review_packages(verb):
    """Ordered package identities, exactly the pinned recipe manifest."""
    review = review_meta(verb)
    result = []
    for name, sha256 in COMPONENTS_METADATA[verb]['recipe_files']:
        if name not in review['packages'] or not re.fullmatch(r'[A-Za-z0-9_.-]+', name):
            raise ValueError('Review package table differs from the pinned recipe manifest')
        size, license_bytes, license_sha256 = review['packages'][name]
        url = review.get('urls', {}).get(name) or (review['url_prefix'] + name)
        result.append({'name': name, 'size': size, 'sha256': sha256,
                       'license_bytes': license_bytes, 'license_sha256': license_sha256,
                       'url': url})
    if len(result) != len(review['packages']):
        raise ValueError('Review package table differs from the pinned recipe manifest')
    return result


def review_total_bytes(verb):
    return sum(package['size'] for package in review_packages(verb))


def check_review_url(url, suffixes):
    parts = urllib.parse.urlsplit(url)
    host = (parts.hostname or '').lower()
    if (parts.scheme != 'https' or parts.username or parts.password or
            parts.port not in (None, 443) or
            not any(host == suffix or host.endswith('.' + suffix) for suffix in suffixes)):
        raise ValueError('Package request left the reviewed HTTPS source policy')


class StrictRedirect(urllib.request.HTTPRedirectHandler):
    def __init__(self, suffixes):
        self.suffixes = tuple(suffixes)

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_review_url(newurl, self.suffixes)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def _pe_unpack(data, offset, fmt):
    if offset < 0 or offset + struct.calcsize(fmt) > len(data):
        raise ValueError('Malformed package: truncated PE structure')
    return struct.unpack_from(fmt, data, offset)


def pe_resource(data, rtype, name, language, limit=PE_MAX_RESOURCE_BYTES):
    """Return exactly one PE resource by static, bounded parsing.

    The input is untrusted: every offset, count and size is range-checked, the
    resource tree depth is fixed at three levels (no loops) and ambiguous or
    empty matches fail closed. Nothing is loaded or executed.
    """
    if len(data) > REVIEW_MAX_PACKAGE_BYTES or data[:2] != b'MZ':
        raise ValueError('Package is not a bounded PE file')
    pe = _pe_unpack(data, 0x3c, '<I')[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Package has no PE header')
    _, sections, _, _, _, optional_size, _ = _pe_unpack(data, pe + 4, '<HHIIIHH')
    if not 1 <= sections <= PE_MAX_SECTIONS:
        raise ValueError('Package has an unexpected PE section count')
    optional = pe + 24
    # The declared optional header must fit in the file before any field is read
    # from it; otherwise bytes past the header could be reinterpreted as header
    # fields (a zero/short SizeOfOptionalHeader would otherwise slip through).
    if optional_size < 2 or optional + optional_size > len(data):
        raise ValueError('Package optional header is truncated')
    magic = _pe_unpack(data, optional, '<H')[0]
    if magic not in (0x10b, 0x20b):
        raise ValueError('Package has an unexpected PE format')
    # The data-directory table follows the layout-specific fixed fields; its count
    # field and the resource entry (index 2) must lie inside the declared header.
    dir_base = 96 if magic == 0x10b else 112
    if optional_size < dir_base + 3 * 8:
        raise ValueError('Package optional header is shorter than its data directories')
    directories = optional + dir_base
    if _pe_unpack(data, directories - 4, '<I')[0] < 3:
        raise ValueError('Package has no resource directory')
    resource_rva, resource_size = _pe_unpack(data, directories + 16, '<II')
    table = optional + optional_size
    if table + 40 * sections > len(data):
        raise ValueError('Package section table exceeds the file')
    spans = []
    for index in range(sections):
        virtual_size, virtual, raw_size, raw = _pe_unpack(data, table + 40 * index + 8, '<IIII')
        if raw + raw_size > len(data):
            raise ValueError('Package section lies outside the file')
        if raw_size and not raw:
            # A non-empty section at file offset zero would map onto the DOS header.
            raise ValueError('Package section points into the DOS header')
        spans.append((virtual, raw, raw_size))
    for first in range(len(spans)):
        for second in range(first + 1, len(spans)):
            start_a, _, size_a = spans[first]
            start_b, _, size_b = spans[second]
            if size_a and size_b and start_a < start_b + size_b and start_b < start_a + size_a:
                # Overlapping VA ranges make RVA-to-file mapping ambiguous.
                raise ValueError('Package sections overlap in virtual address space')

    def locate(rva, length):
        for virtual, raw, raw_size in spans:
            if virtual <= rva < virtual + raw_size:
                start = raw + rva - virtual
                if start + length <= raw + raw_size:
                    return start
        raise ValueError('Package resource lies outside its section')

    if not resource_rva or not 16 <= resource_size <= len(data):
        raise ValueError('Package has an invalid resource directory')
    base = locate(resource_rva, resource_size)

    def entries(relative):
        if not 0 <= relative < resource_size:
            raise ValueError('Resource directory offset is out of range')
        _, _, _, _, named, ids = _pe_unpack(data, base + relative, '<IIHHHH')
        if named + ids > PE_MAX_RESOURCE_ENTRIES or relative + 16 + 8 * (named + ids) > resource_size:
            raise ValueError('Resource directory is too large')
        for index in range(named + ids):
            ident, target = _pe_unpack(data, base + relative + 16 + 8 * index, '<II')
            if ident & 0x80000000:
                at = ident & 0x7fffffff
                if at + 2 > resource_size:
                    raise ValueError('Resource name is out of range')
                length = _pe_unpack(data, base + at, '<H')[0]
                if not 0 < length <= 64 or at + 2 + 2 * length > resource_size:
                    raise ValueError('Resource name is malformed')
                key = bytes(data[base + at + 2:base + at + 2 + 2 * length]).decode('utf-16-le', 'strict')
            else:
                key = ident
            yield key, target

    def find(relative, key, want_directory):
        found = [target for candidate, target in entries(relative) if candidate == key]
        if len(found) != 1:
            raise ValueError('Package resource is missing or ambiguous')
        target = found[0]
        if bool(target & 0x80000000) != want_directory:
            raise ValueError('Package resource tree has an unexpected shape')
        return target & 0x7fffffff

    level_two = find(0, rtype, True)
    level_three = find(level_two, name, True)
    leaf = find(level_three, language, False)
    if not 0 <= leaf <= resource_size - 16:
        raise ValueError('Resource data entry is out of range')
    rva, size, _, _ = _pe_unpack(data, base + leaf, '<IIII')
    if not 0 < size <= limit:
        raise ValueError('Package license resource is empty or oversized')
    start = locate(rva, size)
    return bytes(data[start:start + size])


def decode_embedded_agreement(raw, encoding):
    """CP1252 resource -> text. Unsupported decodings and controls fail closed."""
    if encoding != 'cp1252':
        raise ValueError('Unsupported license encoding')
    body = raw.rstrip(b'\0')
    if not body or b'\0' in body:
        raise ValueError('Unexpected NUL in embedded license')
    text = body.decode('cp1252', errors='strict').replace('\r\n', '\n').replace('\r', '\n')
    text = text.replace('\f', '\n')
    if any(ord(char) < 32 and char not in '\n\t' for char in text):
        raise ValueError('Unexpected control character in embedded license')
    text = text.strip('\n') + '\n'
    if len(text.strip()) < 200:
        raise ValueError('Embedded license is too short to be complete')
    return text


def render_embedded_terms(verb, agreements, enforce_pin=True):
    """Ordered, complete rendering. Identical agreements are shown once, with the
    packages they apply to; distinct agreements are never merged or dropped."""
    review = review_meta(verb)
    groups = []
    for name, text in agreements:
        for group in groups:
            if group['text'] == text:
                group['packages'].append(name)
                break
        else:
            groups.append({'text': text, 'packages': [name]})
    title = COMPONENTS_METADATA[verb]['title']
    out = [f'{title} - license agreements read from the downloaded packages.\n',
           f'{len(groups)} distinct agreement(s) apply to {len(agreements)} package(s). '
           'Nothing has been installed by reviewing these terms.\n']
    for index, group in enumerate(groups, 1):
        out.append(f'\n===== Agreement {index} of {len(groups)} - applies to: '
                   + ', '.join(group['packages']) + ' =====\n')
        out.append(group['text'])
    text = ''.join(out)
    if len(text.encode('utf-8')) > MAX_TERMS_BYTES or '\x00' in text:
        raise ValueError('Rendered terms are oversized or malformed')
    pinned = review.get('rendered_sha256')
    if enforce_pin and (not pinned or hashlib.sha256(text.encode('utf-8')).hexdigest() != pinned):
        raise ValueError('Rendered embedded terms differ from the reviewed text')
    return text


def embedded_fingerprint(verb, text):
    """Consent fingerprint: component/version, every package + recipe + policy
    (component_identity), each package's raw agreement identity, rendered text."""
    review = review_meta(verb)
    raw = ' '.join(f'{package["name"]}:{package["license_sha256"]}' for package in review_packages(verb))
    return hashlib.sha256('\n'.join((component_identity(verb), review['policy'], raw, text)).encode('utf-8')).hexdigest()


def rtf_to_text(text):
    for _ in range(5):
        text = re.sub(r'\{\\\*(?:[^{}]|\{[^{}]*\})*\}', '', text)
        text = re.sub(r'\{\\(?:fonttbl|colortbl|stylesheet)(?:[^{}]|\{[^{}]*\})*\}', '', text)
        text = re.sub(r'\{\\\*?[a-zA-Z]+[^{}]*\}', '', text)
    text = re.sub(r'\{\\field\{\\\*\\fldinst\{HYPERLINK "([^"]+)"\}\}\{\\fldrslt\{([^}]+)\}\}\}', r'\2 (\1)', text)
    text = re.sub(r'\\par[d]?\b', '\n', text)
    text = re.sub(r'\\line\b', '\n', text)
    text = re.sub(r'\\tab\b', '  ', text)
    text = text.replace(r'\~', ' ')
    text = text.replace(r'\ldblquote', '"').replace(r'\rdblquote', '"')
    text = text.replace(r'\lquote', "'").replace(r'\rquote', "'")
    text = text.replace(r'\bullet', '\u2022')
    def replace_hex(m):
        val = int(m.group(1), 16)
        if val == 0xb7:
            return '\u2022'
        return bytes([val]).decode('cp1252', errors='replace')
    text = re.sub(r"\\'([0-9a-fA-F]{2})", replace_hex, text)
    text = re.sub(r'\\[a-zA-Z]+-?[0-9]*[ ]?', '', text)
    text = text.replace('{', '').replace('}', '')
    text = re.sub(r'\n[ \t]*\n[ \t]*\n+', '\n\n', text)
    return text.strip()


def render_d3dcompiler_terms(raw_rtf, enforce_pin=True):
    if isinstance(raw_rtf, bytes):
        raw_rtf = raw_rtf.decode('cp1252', errors='replace')
    intro_start = raw_rtf.find('MICROSOFT SOFTWARE LICENSE TERMS')
    adk_start = raw_rtf.find('MICROSOFT WINDOWS ASSESSMENT AND DEPLOYMENT KIT')
    sdk_start = raw_rtf.find('MICROSOFT WINDOWS SOFTWARE DEVELOPMENT KIT (SDK)')
    if intro_start == -1 or adk_start == -1 or sdk_start == -1:
        raise ValueError('Could not locate required sections in d3dcompiler license RTF')
    intro_txt = rtf_to_text(raw_rtf[intro_start:adk_start])
    sdk_txt = rtf_to_text(raw_rtf[sdk_start:])
    combined = intro_txt + '\n\n' + ('=' * 70) + '\n\n' + sdk_txt + '\n'
    if len(combined.encode('utf-8')) > MAX_TERMS_BYTES or '\x00' in combined:
        raise ValueError('Rendered d3dcompiler terms are oversized or malformed')
    pinned = COMPONENTS_METADATA['d3dcompiler_47']['review']['rendered_sha256']
    if enforce_pin and (not pinned or hashlib.sha256(combined.encode('utf-8')).hexdigest() != pinned):
        raise ValueError('Rendered d3dcompiler terms differ from the reviewed text')
    return combined


def d3dcompiler_terms_from_cache(verb, cache_dir=None):
    review = review_meta(verb)
    cache_dir = cache_dir or REVIEW_CACHE / verb
    _review_directory_ok(cache_dir)
    for package in review_packages(verb):
        path = cache_dir / package['name']
        if not _package_valid(path, package):
            raise ReviewRequired(f'{package["name"]} is missing or changed; download it again for review')
    sdksetup = cache_dir / 'sdksetup.exe'
    raw_rtf = subprocess.check_output(['/usr/bin/cabextract', '-p', '-F', 'u1', str(sdksetup)])
    expected_bytes, expected_hash = review['packages']['sdksetup.exe'][1], review['packages']['sdksetup.exe'][2]
    if len(raw_rtf) != expected_bytes or hashlib.sha256(raw_rtf).hexdigest() != expected_hash:
        raise ValueError('sdksetup.exe embedded license RTF differs from the reviewed identity')
    text = render_d3dcompiler_terms(raw_rtf)
    return text, embedded_fingerprint(verb, text)


def _review_directory_ok(path, create=False):
    privileged = os.getuid() == 0
    if create:
        path.mkdir(mode=0o700, parents=True, exist_ok=True)
    for entry in (path, *path.parents):
        info = entry.lstat()
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
            raise ValueError(f'Insecure review cache path: {entry}')
        if privileged and (info.st_uid != 0 or info.st_mode & 0o022):
            raise ValueError(f'Insecure review cache path: {entry}')
    if privileged and path.lstat().st_mode & 0o077:
        raise ValueError('Review cache directory must be private')


def _package_valid(path, package):
    try:
        info = path.lstat()
    except FileNotFoundError:
        return False
    if (not stat.S_ISREG(info.st_mode) or info.st_size != package['size'] or
            (os.getuid() == 0 and (info.st_uid != 0 or info.st_mode & 0o022))):
        return False
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 16), b''):
            digest.update(chunk)
    return digest.hexdigest() == package['sha256']


def embedded_terms_from_cache(verb, cache_dir=None):
    """Re-verify every cached package and statically re-extract the terms."""
    if verb == 'd3dcompiler_47':
        return d3dcompiler_terms_from_cache(verb, cache_dir)
    review = review_meta(verb)
    cache_dir = cache_dir or REVIEW_CACHE / verb
    _review_directory_ok(cache_dir)
    resource = review['resource']
    agreements = []
    for package in review_packages(verb):
        path = cache_dir / package['name']
        if not _package_valid(path, package):
            raise ReviewRequired(f'{package["name"]} is missing or changed; download it again for review')
        raw = pe_resource(path.read_bytes(), resource['type'], resource['name'], resource['language'])
        if len(raw) != package['license_bytes'] or hashlib.sha256(raw).hexdigest() != package['license_sha256']:
            raise ValueError(f'{package["name"]} embedded agreement differs from the reviewed identity')
        agreements.append((package['name'], decode_embedded_agreement(raw, resource['encoding'])))
    text = render_embedded_terms(verb, agreements)
    return text, embedded_fingerprint(verb, text)


def review_state(verb, directory=None):
    """('none'|'stale'|'unverified'|'cached', total_bytes).

    'cached' means strict revalidation of every cached package against the
    rendered document succeeded — the same check a root GET_TERMS performs. It
    never means installed or accepted. The not-verified states are honest,
    distinct outcomes and must never be advertised as cached:
      'none'       no terms document was downloaded (ReviewRequired);
      'stale'      the cached document no longer matches the pinned identity,
                   policy, or rendered text (StaleTermsError), or the packages
                   re-verify to a different fingerprint;
      'unverified' a consistent document exists but its package cache could not
                   be independently re-verified here — a missing/tampered
                   package, or an unprivileged reader of the private cache.
    """
    total = review_total_bytes(verb)
    try:
        _, fingerprint = embedded_license_text(verb, directory, strict=False)
    except ReviewRequired:
        return 'none', total
    except StaleTermsError:
        return 'stale', total
    # The document is present and identity/fingerprint-consistent. Advertise
    # 'cached' only if the package cache still re-verifies to that same
    # fingerprint — exactly what strict retrieval (root GET_TERMS) requires. A
    # missing, tampered, or unreadable (unprivileged) package cache is an honest
    # not-verified state; it is never silently refreshed and never 'cached'.
    try:
        _, current = embedded_terms_from_cache(verb)
    except Exception:
        return 'unverified', total
    return ('cached' if current == fingerprint else 'stale'), total


def embedded_license_text(verb, directory=None, cache_dir=None, strict=None):
    """Cached, verified terms. Raises ReviewRequired if nothing was downloaded.

    Root recomputes everything from the verified package cache (strict), so an
    edited JSON text cannot authorize an installation.
    """
    directory = directory or TERMS
    path = directory / (validate_verb(verb) + '.json')
    if not path.is_file():
        raise ReviewRequired(f'{verb} package has not been downloaded for review; no terms are available')
    for entry in (path, *path.parents):
        if entry.is_symlink() or entry.stat().st_uid != 0 or entry.stat().st_mode & 0o022:
            raise ValueError(f'Insecure vendor license path: {entry}')
    if path.stat().st_size > MAX_TERMS_BYTES:
        raise ValueError('Oversized cached terms')
    try:
        record = json.loads(path.read_text(encoding='utf-8'))
    except ValueError as error:
        # A truncated or mis-encoded cached document is damaged data, not a
        # verified cache and never grounds an implicit download. It maps to
        # 'stale' so the normal re-review flow recovers it; the integrity/size
        # checks above stay hard errors. JSONDecodeError and UnicodeDecodeError
        # are both ValueError subclasses.
        raise StaleTermsError('Cached terms document is damaged; download for review again') from error
    if not isinstance(record, dict):
        raise StaleTermsError('Cached terms document is damaged; download for review again')
    text = record.get('text', '')
    fingerprint = record.get('fingerprint')
    if not isinstance(text, str) or not isinstance(fingerprint, str):
        # A wrong text/fingerprint type would otherwise escape as an uncaught
        # AttributeError/KeyError and suppress the whole catalog; classify it as
        # damaged so other component rows stay accessible.
        raise StaleTermsError('Cached terms document is damaged; download for review again')
    try:
        text.encode('utf-8')
    except UnicodeEncodeError as error:
        # Valid JSON may carry an escaped lone surrogate (e.g. "\ud800"), which
        # json.loads decodes into a str that cannot be re-encoded to UTF-8. The
        # fingerprint encode below would otherwise raise an uncaught
        # UnicodeEncodeError out of review_state and suppress the whole catalog;
        # classify it as damaged so siblings stay accessible and strict GET_TERMS
        # fails closed. Path/owner/mode/size and package-cache checks are untouched.
        raise StaleTermsError('Cached terms document is damaged; download for review again') from error
    if record.get('schema') != REVIEW_TERMS_SCHEMA or record.get('kind') != 'embedded-review':
        # Old consent/cache schemas are invalidated, never migrated. A download
        # authorization is not stored, so none can be migrated into acceptance.
        raise StaleTermsError('Cached terms use an old schema; download for review again')
    if (record.get('component_identity') != component_identity(verb) or
            hashlib.sha256(text.encode('utf-8')).hexdigest() != review_meta(verb).get('rendered_sha256') or
            fingerprint != embedded_fingerprint(verb, text)):
        raise StaleTermsError('Cached document/package identity changed; download for review again')
    if strict is None:
        strict = os.getuid() == 0 and directory == TERMS
    if strict:
        _, current = embedded_terms_from_cache(verb, cache_dir)
        if current != fingerprint:
            raise StaleTermsError('Package cache no longer matches the reviewed terms')
    return text, fingerprint


def cleanup_review_cache(verb, cache_dir=None, now=None):
    """Remove partial downloads and packages older than the retention period."""
    cache_dir = cache_dir or REVIEW_CACHE / verb
    now = time.time() if now is None else now
    removed = []
    if not cache_dir.is_dir() or cache_dir.is_symlink():
        return removed
    for entry in cache_dir.iterdir():
        info = entry.lstat()
        if entry.name.endswith('.part') or now - info.st_mtime > REVIEW_RETENTION_SECONDS:
            if stat.S_ISDIR(info.st_mode) and not stat.S_ISLNK(info.st_mode):
                continue
            entry.unlink()
            removed.append(entry.name)
    return removed


@contextmanager
def _review_download_lock(cache_dir, cancel=None):
    """Serialize review downloads per cache directory.

    The coordinator's START_REVIEW thread and a later pkexec --review-download
    process can target the same cache; without this they race on the O_EXCL
    partial file and on publication. The lock lives beside, never inside, the
    package cache so retention cleanup cannot remove a held lock.

    ``cancel`` (the threading.Event passed to review_download) makes acquisition
    deadline- AND cancellation-aware: a cancelled download stops waiting for the
    lock instead of blocking up to REVIEW_LOCK_WAIT_SECONDS and then proceeding
    to write into a cache its caller has abandoned.
    """
    lock_path = cache_dir.parent / (cache_dir.name + '.lock')
    descriptor = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    locked = False
    try:
        deadline = time.monotonic() + REVIEW_LOCK_WAIT_SECONDS
        while True:
            if cancel is not None and cancel.is_set():
                raise ReviewCancelled()
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                locked = True
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise OSError('Another review download is active; try again later')
                time.sleep(0.1)
        yield
    finally:
        if locked:
            fcntl.flock(descriptor, fcntl.LOCK_UN)
        os.close(descriptor)


def review_download(verb, cache_dir=None, terms_dir=None, progress=None, cancel=None,
                    opener=None, trace=None):
    """Download the pinned packages for terms review, then extract the terms.

    Only catalog-derived URLs are requested; frontend-supplied paths, URLs and
    member names are not inputs. The result is cached, never accepted.
    """
    review = review_meta(verb)
    packages = review_packages(verb)
    privileged_defaults = cache_dir is None and terms_dir is None
    if privileged_defaults and os.getuid() != 0:
        raise PermissionError('Downloading packages for review requires the privileged backend')
    cache_dir = cache_dir or REVIEW_CACHE / verb
    terms_dir = terms_dir or TERMS
    _review_directory_ok(cache_dir, create=True)
    with _review_download_lock(cache_dir, cancel=cancel):
        if cancel is not None and cancel.is_set():
            raise ReviewCancelled()
        cleanup_review_cache(verb, cache_dir)
        total = sum(package['size'] for package in packages)
        if shutil.disk_usage(cache_dir).free < total + (64 << 20):
            raise OSError('Not enough free disk space to download the packages for review')
        opener = opener or urllib.request.build_opener(StrictRedirect(review['host_suffixes']))
        deadline = time.monotonic() + REVIEW_DEADLINE_SECONDS
        done = 0
        for package in packages:
            path = cache_dir / package['name']
            if _package_valid(path, package):
                done += package['size']
                if progress:
                    progress(done, total)
                continue
            if os.path.lexists(path):
                path.unlink()
            check_review_url(package['url'], review['host_suffixes'])
            request = urllib.request.Request(package['url'], headers={'User-Agent': 'WineExplorer-components/1'})
            if trace:
                trace('GET', package['url'], package['size'])
            part = cache_dir / (package['name'] + '.part')
            descriptor = os.open(part, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
            try:
                with os.fdopen(descriptor, 'wb') as out, \
                        opener.open(request, timeout=REVIEW_READ_TIMEOUT) as response:
                    check_review_url(getattr(response, 'url', package['url']), review['host_suffixes'])
                    headers = getattr(response, 'headers', None)
                    declared = headers.get('Content-Length') if headers is not None else None
                    if declared is not None and int(declared) != package['size']:
                        raise ValueError(f'{package["name"]} size differs from the reviewed package')
                    digest = hashlib.sha256()
                    received = 0
                    while True:
                        if cancel is not None and cancel.is_set():
                            raise ReviewCancelled()
                        if time.monotonic() > deadline:
                            raise TimeoutError('Package download exceeded its time limit')
                        chunk = response.read(1 << 16)
                        if not chunk:
                            break
                        received += len(chunk)
                        if received > package['size']:
                            raise ValueError(f'{package["name"]} is larger than the reviewed package')
                        digest.update(chunk)
                        out.write(chunk)
                        if progress:
                            progress(done + received, total)
                    if cancel is not None and cancel.is_set():
                        raise ReviewCancelled()
                    if received != package['size']:
                        raise ValueError(f'{package["name"]} download was truncated')
                    if digest.hexdigest() != package['sha256']:
                        raise ValueError(f'{package["name"]} does not match the reviewed package hash')
                    out.flush()
                    os.fsync(out.fileno())
                os.replace(part, path)
            except BaseException:
                part.unlink(missing_ok=True)
                raise
            done += package['size']
        if cancel is not None and cancel.is_set():
            raise ReviewCancelled()
        text, fingerprint = embedded_terms_from_cache(verb, cache_dir)
        if cancel is not None and cancel.is_set():
            raise ReviewCancelled()
        if os.getuid() == 0 or terms_dir != TERMS:
            terms_dir.mkdir(parents=True, exist_ok=True)
            if os.getuid() == 0:
                for entry in (terms_dir, *terms_dir.parents):
                    if entry.is_symlink() or entry.stat().st_uid != 0 or entry.stat().st_mode & 0o022:
                        raise ValueError(f'Insecure vendor license path: {entry}')
            atomic_json(terms_dir / (verb + '.json'),
                        {'schema': REVIEW_TERMS_SCHEMA, 'kind': 'embedded-review',
                         'component_identity': component_identity(verb),
                         'rendered_sha256': review['rendered_sha256'],
                         'fingerprint': fingerprint, 'text': text}, 0o644)
        return text, fingerprint


SYSRUN_CONF = Path('/etc/wineexplorer/sysrun.conf')
# the shared protected reader of sysrun.conf (wine/include/wine/wex_sysrun_conf.h); its rules are not repeated here
SYSRUN_CONF_READER = '/usr/local/libexec/wineexplorer/wex-is-admin'
# winetricks' "$WINESERVER -w/-k" for an install that runs as a client of session 0 (see MachineWine)
MAINTENANCE_WINESERVER = '/usr/local/libexec/wineexplorer/wex-maint-wineserver'
HIVE_SAVE_TIMEOUT = 20      # session 0 saves a changed hive every 5 seconds (server/registry.c)
IN_CLOSE_WRITE, IN_MOVED_TO = 0x08, 0x80


def sysrun_value(key, conf=None):
    """KEY of the protected sysrun.conf, read by the shared C reader: None when the file or the key is absent. A file
    that is there but invalid or unprotected raises: maintenance never falls back to the defaults for it."""
    conf = Path(conf or SYSRUN_CONF)
    if not os.path.lexists(conf):
        return None
    result = subprocess.run([SYSRUN_CONF_READER, '--print-value', key, str(conf)],
                            capture_output=True, text=True, timeout=30)
    if result.returncode == 3:
        return None
    if result.returncode or not result.stdout.endswith('\n'):
        raise ValueError(f'{conf} is invalid or unprotected; machine maintenance refused')
    return result.stdout[:-1]


def wait_for_newer(directory, name, since_ns, timeout):
    """Wait until DIRECTORY/NAME was written after SINCE_NS (inotify on the directory: Wine saves a hive into a new
    file and renames it into place). True when it was, False at TIMEOUT. Only metadata is read, nothing opened."""
    libc = ctypes.CDLL(None, use_errno=True)
    fd = libc.inotify_init1(os.O_CLOEXEC | os.O_NONBLOCK)
    if fd < 0:
        raise OSError(ctypes.get_errno(), 'inotify_init1')
    try:
        if libc.inotify_add_watch(fd, os.fsencode(directory), IN_CLOSE_WRITE | IN_MOVED_TO) < 0:
            raise OSError(ctypes.get_errno(), 'inotify_add_watch', str(directory))
        deadline = time.monotonic() + timeout
        while True:
            try:
                if os.stat(Path(directory) / name, follow_symlinks=False).st_mtime_ns > since_ns:
                    return True
            except FileNotFoundError:
                pass
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            if select.select([fd], [], [], remaining)[0]:
                os.read(fd, 65536)
    finally:
        os.close(fd)


class MachineWine:
    """Wine on the machine root for component maintenance (docs/KNOWN_BUGS.md B11 and B34).

    With session 0 (session0_socket in sysrun.conf) every Wine program of an install runs as session 0's account,
    in a transient systemd unit, as a client of session 0's running server. Root never runs Wine on the machine
    root, whose files and registry overrides the whole system group can change, and the machine registry is written
    through session 0 only (no second server saves system.reg). The unit's cgroup holds exactly this install's
    programs: systemd ends what is left when the main program returns, and MAINTENANCE_WINESERVER gives winetricks'
    `wineserver -w` / `-k` that meaning, since session 0 never goes idle and is not the install's to stop. The
    account reads nothing of root's: the verified payloads are copied into a run directory of its own (stage()), and
    root never reads that copy back.

    Without session 0 the earlier way stays: root runs Wine with a server of its own (B34 is open for that case)."""

    WINE = '/usr/local/bin/wine'
    WINESERVER = '/usr/local/bin/wineserver'
    PATH = '/usr/local/bin:/usr/bin:/bin'

    def __init__(self, machine=None, conf=None):
        self.machine = Path(machine or MACHINE)
        self.run_dir = None
        self.w_cache = None
        self.started_ns = None
        self.finished_ns = None
        socket_path = sysrun_value('session0_socket', conf)
        self.session0 = socket_path is not None
        if self.session0:
            account = sysrun_value('session0_user', conf)
            if not account:
                raise ValueError('sysrun.conf names session 0 but not its account (session0_user)')
            self.account = pwd.getpwnam(account)
            if self.account.pw_uid == 0:
                raise ValueError('Session 0 must not run as root')
            self.socket = Path(socket_path)

    def __enter__(self):
        if self.session0:
            self.run_dir = self._make_run_dir()
        return self

    def __exit__(self, *exc):
        if self.run_dir is not None:
            # the account's own tree: removed through descriptors (a link put in it is removed, never followed)
            assert shutil.rmtree.avoids_symlink_attacks
            shutil.rmtree(self.run_dir, ignore_errors=True)
            self.run_dir = None

    def _make_run_dir(self):
        """A private directory for this run, made by root in a root-only parent and only traversable by others; its
        home/ is the account's. Nothing in it can be in root's way: root creates every name in it itself."""
        base = self.machine.parent / 'component-runs'
        base.mkdir(mode=0o755, exist_ok=True)
        info = base.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError(f'Untrusted component run directory: {base}')
        run = Path(tempfile.mkdtemp(prefix='run-', dir=base))
        os.chmod(run, 0o711)
        os.mkdir(run / 'home', 0o700)
        os.chown(run / 'home', self.account.pw_uid, self.account.pw_gid, follow_symlinks=False)
        return run

    @property
    def file_owner(self):
        """Who the install's programs run as: session 0's account, else root."""
        return self.account.pw_uid if self.session0 else 0

    def stage(self, *sources):
        """SOURCES, verified files or directories of root's cache, as the install sees them: the directory that holds
        them (winetricks' W_CACHE from then on). With session 0 a copy for the account: root copies into a new
        directory of its own that nobody else can enter, then hands it over by descriptor, and never reads it back.
        Without session 0 the sources stay where they are (in CACHE)."""
        if not self.session0:
            return CACHE
        self.w_cache = self._hand_over(sources)
        return self.w_cache

    def _hand_over(self, sources=()):
        """A new directory of the account's in the run directory holding copies of SOURCES. Root makes and fills it
        while nobody else can enter it, then changes the owners through descriptors (no name is followed)."""
        directory = Path(tempfile.mkdtemp(prefix='stage-', dir=self.run_dir))
        for source in map(Path, sources):
            if source.is_dir() and not source.is_symlink():
                shutil.copytree(source, directory / source.name, symlinks=True)
            else:
                shutil.copy2(source, directory / source.name, follow_symlinks=False)
        uid, gid = self.account.pw_uid, self.account.pw_gid
        for _, dirs, files, dir_fd in os.fwalk(directory, follow_symlinks=False):
            for name in dirs + files:
                os.chown(name, uid, gid, dir_fd=dir_fd, follow_symlinks=False)
        os.chown(directory, uid, gid, follow_symlinks=False)
        return directory

    def export_key(self, key, backup):
        """reg export KEY into the root-only file BACKUP. With session 0 the account exports into a directory of its
        own and root copies the result through a descriptor (a regular file only, bounded; never followed)."""
        if not self.session0:
            return self.reg('export', key, str(backup), '/y', check=True)
        directory = self._hand_over()
        result = self.reg('export', key, str(directory / 'export.reg'), '/y', check=True)
        dir_fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            data = machine_read_regular(dir_fd, ['export.reg'], MAX_MACHINE_HIVE_BYTES)
        finally:
            os.close(dir_fd)
        with open(backup, 'xb') as out:
            out.write(data)
        return result

    def import_file(self, path):
        """reg import PATH (a file of root's): with session 0 the account imports a copy handed to it."""
        path = Path(path)
        if self.session0:
            path = self._hand_over([path]) / path.name
        return self.reg('import', str(path), check=True)

    def environment(self):
        """What Wine and winetricks get; nothing of the caller's environment (no prefix, overrides, display)."""
        # WINE64: Wine is a new-style WoW64 build, whose one loader runs 64-bit programs too. winetricks would find that
        # out from the wineserver binary's architecture, which with session 0 is MAINTENANCE_WINESERVER, a script.
        common = {'PATH': self.PATH, 'LANG': 'C.UTF-8', 'WINEPREFIX': str(self.machine), 'WINE': self.WINE,
                  'WINE64': self.WINE, 'WINEDLLOVERRIDES': 'winemenubuilder.exe=d', 'WINEDEBUG': '-all'}
        if not self.session0:
            return dict(common, HOME='/root', USER='root', LOGNAME='root', WINESERVER=self.WINESERVER,
                        W_CACHE=str(CACHE), XDG_CACHE_HOME=str(CACHE.parent))
        home = str(self.run_dir / 'home')
        return dict(common, HOME=home, USER=self.account.pw_name, LOGNAME=self.account.pw_name,
                    WINESERVER=MAINTENANCE_WINESERVER, W_CACHE=str(self.w_cache or self.run_dir / 'home'),
                    XDG_CACHE_HOME=home + '/.cache')

    def command(self, argv, *, timeout, isolate=False):
        """ARGV bounded by TIMEOUT seconds; ISOLATE: without a network. With session 0 inside a transient unit as the
        account (a deadline of systemd's too, past the inner one)."""
        bounded = ['/usr/bin/timeout', '--signal=TERM', '--kill-after=10s', f'{timeout}s', *argv]
        if not self.session0:
            return ['/usr/bin/unshare', '--net', *bounded] if isolate else bounded
        command = ['/usr/bin/systemd-run', '--quiet', '--wait', '--pipe', '--collect', '--service-type=exec',
                   '--unit=wex-components-' + uuid.uuid4().hex[:12],
                   f'--uid={self.account.pw_uid}', f'--gid={self.account.pw_gid}',
                   '--working-directory=' + str(self.run_dir / 'home'),
                   '-p', 'NoNewPrivileges=yes', '-p', f'RuntimeMaxSec={timeout + 60}']
        if isolate:
            command += ['-p', 'PrivateNetwork=yes']
        command += [f'--setenv={key}={value}' for key, value in sorted(self.environment().items())]
        return command + ['--', *bounded]

    def run(self, argv, *, timeout, isolate=False, **kwargs):
        """Run ARGV (see command()). Without session 0 root's environment is the program's; with it systemd-run gets
        a bare one and passes the program its own."""
        self.started_ns = self.started_ns or time.time_ns()
        if not self.session0:
            if isolate:
                # unshare --net isolates only processes started inside it: a server that an earlier program started
                # (host network) would serve the isolated program's sockets. It must end first.
                try:
                    self._server('-w', timeout=60)
                except subprocess.TimeoutExpired:
                    self.stop()
            return subprocess.run(self.command(argv, timeout=timeout, isolate=isolate), env=self.environment(), **kwargs)
        try:
            return subprocess.run(self.command(argv, timeout=timeout, isolate=isolate),
                                  env={'PATH': self.PATH, 'LANG': 'C.UTF-8'}, timeout=timeout + 90, **kwargs)
        finally:
            # what this program changed is in session 0's server now: a save of system.reg after this moment holds
            # it (the server saves between requests, so a save that ends later began after the program's last one)
            self.finished_ns = time.time_ns()

    def reg(self, *args, check=False):
        argv = [self.WINE, 'reg.exe', *args]
        if not self.session0:
            self.started_ns = self.started_ns or time.time_ns()
            return subprocess.run(argv, env=self.environment(), capture_output=True, timeout=90, check=check)
        return self.run(argv, timeout=90, capture_output=True, check=check)

    def _server(self, *args, timeout, check=True):
        return subprocess.run([self.WINESERVER, *args], env=self.environment(), timeout=timeout, check=check)

    def require_idle(self):
        """Before maintenance: without session 0 no Wine may run on the machine root (root's server must be gone);
        with it session 0 must be serving (its programs keep running: they are not the install's)."""
        if not self.session0:
            self._server('-w', timeout=2)
            return
        try:
            serving = stat.S_ISSOCK(self.socket.lstat().st_mode)
        except FileNotFoundError:
            serving = False
        if not serving:
            raise ValueError('Session 0 is not running (wex-session0.service); components install through it')
        # an earlier install's programs outlive a killed backend in their unit: none may still be running
        units = subprocess.run(['/usr/bin/systemctl', 'list-units', '--no-legend', '--plain',
                                '--state=active,activating,deactivating', 'wex-components-*.service'],
                               capture_output=True, text=True, timeout=30, check=True)
        if units.stdout.strip():
            raise ValueError('An earlier component install is still running: ' + units.stdout.split()[0])

    def wait_idle(self, timeout=120):
        """After Wine programs that changed the machine: their changes are in system.reg. Without session 0 root's
        server has exited; with it session 0 has saved the hive since the last program of this run ended (when
        nothing changed it saves nothing: the verification that follows decides). Not since the first one started:
        session 0 saves during a long installer, and the writes after it (WebView2's update policy) would be missing
        from the file the verification reads. Nothing to wait for when no program ran."""
        if self.started_ns is None:
            return
        if not self.session0:
            self._server('-w', timeout=timeout)
            return
        wait_for_newer(self.machine, 'system.reg', self.finished_ns or self.started_ns, min(timeout, HIVE_SAVE_TIMEOUT))

    def stop(self, check=True):
        """End what is left of an install. Without session 0 root's whole server; with it nothing is left to end:
        systemd ended the unit's programs with the unit."""
        if not self.session0:
            self._server('-k', timeout=15, check=check)
            self._server('-w', timeout=30, check=check)


WINE_LIBRARIES = Path('/usr/local/lib/wine')
BUILTIN_LIBRARY_DIRS = ('i386-windows', 'x86_64-windows')


def builtin_link_source(machine, directory, link, libraries=WINE_LIBRARIES):
    """The installed Wine file that the library link LINK of windows/DIRECTORY names, when it resolves inside
    LIBRARIES/{i386,x86_64}-windows. A link to anything else (planted by a member of the machine group: a root-only
    file, a device) is refused, so that root never copies it into the machine root."""
    resolved = Path(os.path.realpath(os.path.join(machine / 'drive_c/windows' / directory, link)))
    real = Path(os.path.realpath(libraries))
    if not any(resolved.parent.is_relative_to(real / arch) for arch in BUILTIN_LIBRARY_DIRS):
        raise ValueError(f'Unexpected builtin DLL symlink: {directory}/{link}')
    return resolved


def snapshot_system_entry(dir_fd, name, backup, path):
    """Copy the entry NAME of a system directory into the root-only snapshot BACKUP: a link as a link, a regular file
    through its descriptor (mode without set-id bits, times). False when there is nothing by that name; a directory, a
    FIFO or a device is refused."""
    try:
        info = os.stat(name, dir_fd=dir_fd, follow_symlinks=False)
    except FileNotFoundError:
        return False
    if stat.S_ISLNK(info.st_mode):
        os.symlink(os.readlink(name, dir_fd=dir_fd), backup)
        return True
    if stat.S_ISDIR(info.st_mode):
        raise ValueError(f'Unexpected protected DLL directory: {path}')
    fd = machine_open_regular(dir_fd, [name])
    try:
        opened = os.fstat(fd)
        total = 0
        with os.fdopen(fd, 'rb', closefd=False) as source, open(backup, 'xb') as out:
            while chunk := source.read(1 << 20):
                total += len(chunk)
                if total > MAX_MACHINE_HASH_BYTES:
                    raise ValueError(f'Oversized protected DLL: {path}')
                out.write(chunk)
        os.chmod(backup, stat.S_IMODE(opened.st_mode) & 0o775)
        os.utime(backup, ns=(opened.st_atime_ns, opened.st_mtime_ns))
    finally:
        os.close(fd)
    return True


def restore_system_entry(dir_fd, name, backup, gid):
    """Put the snapshot BACKUP back as NAME of a system directory, by descriptor and one rename (a link as a link)."""
    info = os.lstat(backup)
    if stat.S_ISLNK(info.st_mode):
        machine_symlink(dir_fd, name, os.readlink(backup))
        return
    machine_write_file(dir_fd, name, backup.read_bytes(), gid, stat.S_IMODE(info.st_mode) & 0o775)
    os.utime(name, ns=(info.st_atime_ns, info.st_mtime_ns), dir_fd=dir_fd, follow_symlinks=False)


@contextmanager
def protect_builtins(machine, wine, libraries=WINE_LIBRARIES):
    """Preserve core files and override keys in the isolated maintenance prefix. The system directories are worked on
    through descriptors opened without following links (the machine group can rename entries there while root works).
    WINE (a MachineWine) runs reg.exe for the override keys."""
    # Keep the root-only recovery snapshot even if restoration itself fails.
    # Debian 13's Python supports delete=False; these can be removed by an
    # administrator after validating the installed component.
    with tempfile.TemporaryDirectory(prefix='components-protected-', dir=machine.parent, delete=False) as scratch:
        saved = Path(scratch)
        print(f'Protected builtin recovery snapshot: {saved}', file=sys.stderr)
        gid = machine_admin_gid(machine)
        directories = {}
        try:
            for directory in ('system32', 'syswow64'):
                directories[directory] = machine_open_path(machine, ('drive_c', 'windows', directory), gid)
            files = []
            for directory, fd in directories.items():
                names = {name + '.dll' for name in PROTECTED}
                names.update(name for name in os.listdir(fd) if protected_name(name))
                (saved / directory).mkdir(exist_ok=True)
                for name in names:
                    backup = saved / directory / name
                    existed = snapshot_system_entry(fd, name, backup, machine / 'drive_c/windows' / directory / name)
                    files.append((directory, name, backup, existed))
            keys = []
            for hive in ('HKCU', 'HKLM'):
                for suffix in ('DllOverrides', 'AppDefaults'):
                    key = hive + '\\Software\\Wine\\' + suffix
                    backup = saved / (hive + '-' + suffix + '.reg')
                    query = wine.reg('query', key)
                    if query.returncode:
                        # Distinguish "key absent" from a broken Wine invocation.
                        wine.reg('query', hive + '\\Software', check=True)
                        existed = False
                    else:
                        wine.export_key(key, backup)
                        existed = True
                    keys.append((key, backup, existed))
            # A third-party extractor/installer may write through symlinks instead
            # of replacing them. Never expose the installed Wine binaries that way.
            # Only links into the installed Wine's PE directories are detached (copied
            # in place); every one is checked before any is replaced.
            links = []
            for directory, fd in directories.items():
                for name in os.listdir(fd):
                    if Path(name).suffix.lower() != '.dll':
                        continue
                    try:
                        info = os.stat(name, dir_fd=fd, follow_symlinks=False)
                    except FileNotFoundError:
                        continue
                    if not stat.S_ISLNK(info.st_mode):
                        continue
                    link = os.readlink(name, dir_fd=fd)
                    source = builtin_link_source(machine, directory, link, libraries)
                    installed_wine_file(source, read=False)
                    links.append((directory, name, link, source))
            detached_links = []
            for directory, name, link, source in links:
                fd = directories[directory]
                backup = saved / 'builtin-links' / directory / name
                backup.parent.mkdir(parents=True, exist_ok=True)
                os.symlink(link, backup)
                data, info = installed_wine_file(source)
                # the machine root's mode for a DLL (group-writable), not the installed file's 0755, and the owner the
                # install runs as: it may replace the builtin with its own native DLL, as any installer may, and set
                # the file's times (only an owner may). Session 0's account could delete and recreate it anyway.
                machine_write_file(fd, name, data, gid, machine_file_mode(Path(name)), owner=wine.file_owner)
                os.utime(name, ns=(info.st_atime_ns, info.st_mtime_ns), dir_fd=fd, follow_symlinks=False)
                detached_links.append((directory, name, backup, hashlib.sha256(data).hexdigest()))
                del data
            recovery = {'safe_to_restore': True}
            try:
                yield recovery
            finally:
                if not recovery['safe_to_restore']:
                    marker = machine.parent / 'component-recovery-required'
                    marker.write_text(str(saved) + '\n', encoding='utf-8')
                    raise RuntimeError('Cannot establish an idle machine prefix; protected snapshot retained; administrator recovery required: ' + str(saved))
                known = {(directory, name) for directory, name, _, _ in files}
                area = machine_stage_area(machine)
                for directory, fd in directories.items():
                    for name in os.listdir(fd):
                        if (directory, name) not in known and protected_name(name):
                            try:
                                info = os.stat(name, dir_fd=fd, follow_symlinks=False)
                            except FileNotFoundError:
                                continue
                            if stat.S_ISDIR(info.st_mode):
                                raise ValueError(f'Unexpected protected DLL directory: {machine / "drive_c/windows" / directory / name}')
                            machine_remove(fd, name, area)
                for directory, name, backup, existed in files:
                    fd = directories[directory]
                    try:
                        info = os.stat(name, dir_fd=fd, follow_symlinks=False)
                    except FileNotFoundError:
                        info = None
                    if info is not None and stat.S_ISDIR(info.st_mode):
                        raise ValueError(f'Installer replaced protected file with directory: {machine / "drive_c/windows" / directory / name}')
                    if existed:
                        restore_system_entry(fd, name, backup, gid)
                    elif info is not None:
                        try:
                            os.unlink(name, dir_fd=fd)
                        except FileNotFoundError:
                            pass
                for directory, name, backup, digest in detached_links:
                    fd = directories[directory]
                    try:
                        os.stat(name, dir_fd=fd, follow_symlinks=False)
                    except FileNotFoundError:
                        # removed by the installer (a failed MSI's rollback removes what it extracted): the builtin's
                        # link goes back, as it was before the install
                        machine_symlink(fd, name, os.readlink(backup))
                        continue
                    try:
                        copy = machine_open_regular(fd, [name])     # a link or a FIFO: left as it is
                    except ValueError:
                        continue
                    try:
                        current = machine_fd_sha256(copy)
                    except ValueError:
                        current = None
                    finally:
                        os.close(copy)
                    if current == digest:
                        machine_symlink(fd, name, os.readlink(backup))
                # The machine root's HKCU is not a user's HKCU. Restore all installer
                # override side effects instead of propagating them into templates.
                for key, backup, existed in keys:
                    result = wine.reg('delete', key, '/f')
                    if result.returncode:
                        wine.reg('query', key.split('\\Software')[0] + '\\Software', check=True)
                    if existed:
                        wine.import_file(backup)
        finally:
            for descriptor in directories.values():
                os.close(descriptor)


def is_installed(verb, accepted_digest=None, machine=None):
    machine = machine or MACHINE
    receipts_dir = machine.parent / 'component-receipts'
    json_file = receipts_dir / (verb + '.json')
    if json_file.is_file():
        try:
            for path in (receipts_dir, json_file):
                info = path.lstat()
                if path.is_symlink() or info.st_uid != 0 or info.st_mode & 0o022:
                    return False
            if json_file.stat().st_size > 1024 * 1024:
                return False
            data = json.loads(json_file.read_text(encoding='utf-8'))
            if COMPONENTS_METADATA[verb].get('execution_policy') == UWP_XAML_POLICY:
                if (data.get('verb') != verb or data.get('execution_policy') != UWP_XAML_POLICY or
                        data.get('component_identity') != component_identity(verb) or
                        data.get('payload_sha256') != uwp_xaml_payload(verb)['sha256'] or
                        (accepted_digest is not None and data.get('terms_sha256') != accepted_digest)):
                    return False
                verify_uwp_xaml(machine, data)
                return True
            if COMPONENTS_METADATA[verb].get('execution_policy') == POWERSHELL_POLICY:
                if (data.get('verb') != verb or data.get('execution_policy') != POWERSHELL_POLICY or
                        data.get('component_identity') != component_identity(verb) or
                        data.get('payload_sha256') != powershell_payload(verb)['sha256'] or
                        (accepted_digest is not None and data.get('terms_sha256') != accepted_digest)):
                    return False
                verify_powershell(machine, data)
                return True
            if COMPONENTS_METADATA[verb].get('execution_policy') == UWP_FRAMEWORKS_POLICY:
                if (data.get('verb') != verb or data.get('execution_policy') != UWP_FRAMEWORKS_POLICY or
                        data.get('component_identity') != component_identity(verb) or
                        data.get('pins') != [package['sha256'] for _, package in uwp_frameworks_packages(verb)] or
                        (accepted_digest is not None and data.get('terms_sha256') != accepted_digest)):
                    return False
                verify_uwp_frameworks(machine, data)
                return True
            if (data.get('verb') == verb and
                data.get('winetricks_sha256') == APPROVED_WINETRICKS_SHA256 and
                data.get('winetricks_version') == APPROVED_WINETRICKS_VERSION and
                data.get('architecture') == 'wow64' and
                data.get('recipe_files') == [list(item) for item in COMPONENTS_METADATA[verb]['recipe_files']] and
                (accepted_digest is None or data.get('terms_sha256') == accepted_digest)):
                if verb == 'vcrun2022':
                    if data.get('component_identity') != component_identity(verb):
                        return False
                    if data.get('execution_recipe_sha256') != hashlib.sha256(approved_runtime_recipe()).hexdigest():
                        return False
                    verify_runtime_artifacts(machine)
                elif verb == 'corefonts':
                    if data.get('component_identity') != component_identity(verb):
                        return False
                    verify_corefonts_artifacts(machine)
                elif verb == 'd3dcompiler_47':
                    if data.get('component_identity') != component_identity(verb):
                        return False
                    if data.get('execution_recipe_sha256') != hashlib.sha256(approved_d3dcompiler_recipe()).hexdigest():
                        return False
                    verify_d3dcompiler_artifacts(machine)
                elif verb in EDGEUPDATE_APPS:
                    if data.get('component_identity') != component_identity(verb):
                        return False
                    verify_webview2_artifacts(machine, verb)
                return True
        except Exception:
            pass
    # Legacy text hashes lack package/recipe/architecture identity.
    return False


def get_installed_receipts(machine=None):
    machine = machine or MACHINE
    receipts_dir = machine.parent / 'component-receipts'
    if not receipts_dir.is_dir():
        return set()
    installed = set()
    for verb in COMPONENTS:
        if is_installed(verb, machine=machine):
            installed.add(verb)
    return installed


def runtime_defaults_text(text):
    """Seed missing HKCU defaults in an idle Wine hive without launching Wine.

    Keep existing global and application-specific choices byte-for-byte. Wine's
    environment overrides retain their normal precedence; none are imposed here.
    """
    if not text.startswith('WINE REGISTRY Version 2') or '\0' in text:
        raise ValueError('Unsupported user registry format')
    lines = text.splitlines(keepends=True)
    start, end, names = None, len(lines), set()
    active = False
    for index, line in enumerate(lines):
        if line.startswith('['):
            section = line[1:].split(']', 1)[0].casefold()
            selected = section == r'Software\\Wine\\DllOverrides'.casefold()
            if active:
                end = index
            if selected:
                if start is not None:
                    raise ValueError('Duplicate DLL override registry section')
                start = index
            active = selected
        elif active:
            value = re.match(r'^"([^"\\]*)"\s*=', line)
            if value:
                names.add(value[1].casefold().lstrip('*').removesuffix('.dll'))
    wanted = sorted(set().union(*RUNTIME_ARTIFACTS.values()) - names)
    if not wanted:
        return text
    additions = ''.join('"' + name + '"="native,builtin"\n' for name in wanted)
    if start is None:
        return text.rstrip('\n') + '\n\n' + r'[Software\\Wine\\DllOverrides]' + '\n' + additions
    return ''.join(lines[:end]).rstrip('\n') + '\n' + additions + '\n' + ''.join(lines[end:])


def apply_user_defaults():
    """Login-only, unprivileged and idle-prefix operation; never a root installer."""
    if os.getuid() == 0:
        raise ValueError('Runtime user defaults must run as the interactive user')
    if not is_installed('vcrun2022'):
        return
    prefix = Path(os.environ.get('WINEPREFIX', str(Path.home() / '.wine')))
    registry = prefix / 'user.reg'
    if prefix.is_symlink() or prefix.stat().st_uid != os.getuid():
        raise ValueError('User prefix must be owned by the current user')
    info = registry.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid():
        raise ValueError('User registry must be an owned regular file')
    # Do not edit a live hive or start Wine's default desktop before Explorer.
    environment = dict(os.environ, WINEPREFIX=str(prefix))
    subprocess.run(['/usr/local/bin/wineserver', '-w'], env=environment, timeout=2, check=True)
    text = registry.read_text(encoding='utf-8')
    updated = runtime_defaults_text(text)
    if updated == text:
        return
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=prefix,
                                         prefix='.runtime-defaults-', delete=False) as out:
            temporary = Path(out.name)
            out.write(updated)
            out.flush()
            os.fsync(out.fileno())
        shutil.copystat(registry, temporary)
        current = registry.lstat()
        if any(getattr(current, field) != getattr(info, field) for field in
               ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')):
            raise ValueError('User registry changed while preparing runtime defaults')
        os.replace(temporary, registry)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


REGEDIT_MAX_BYTES = 64 * 1024 * 1024


def ensure_installer_regedit(machine, libraries=WINE_LIBRARIES):
    """Repair only missing/empty regedit launchers, never arbitrary native DLLs.

    Old code linked BOTH architectures to windows/regedit.exe, a 64-bit image.
    Use the corresponding installed Wine PE; never write through prefix links.
    The machine root is read and written through descriptors opened without
    following links; a link is followed only to windows/regedit.exe (the legacy
    link) or to the installed Wine's own regedit.exe.
    """
    gid = machine_admin_gid(machine)
    directories = {}
    try:
        directories['windows'] = machine_open_path(machine, ('drive_c', 'windows'), gid)
        replacements = []
        for directory, arch, expected in (('system32', 'x86_64', 0x8664), ('syswow64', 'i386', 0x14c)):
            fd = directories[directory] = machine_open_path(machine, ('drive_c', 'windows', directory), gid)
            source = libraries / (arch + '-windows') / 'regedit.exe'

            def valid_pe(dir_fd, parts):
                try:
                    return pe_machine_is(machine_read_regular(dir_fd, parts, REGEDIT_MAX_BYTES), expected)
                except ValueError:
                    return False

            def checked_source():
                try:
                    data, _ = installed_wine_file(source, REGEDIT_MAX_BYTES)
                except ValueError:
                    raise ValueError('Untrusted installed Wine regedit source') from None
                if not pe_machine_is(data, expected):
                    raise ValueError('Installed Wine regedit has wrong PE architecture')

            try:
                info = os.stat('regedit.exe', dir_fd=fd, follow_symlinks=False)
            except FileNotFoundError:
                info = None
            legacy_link = False
            if info is not None and stat.S_ISLNK(info.st_mode):
                link = os.readlink('regedit.exe', dir_fd=fd)
                legacy_link = link == '../regedit.exe'
                if legacy_link:
                    if valid_pe(directories['windows'], ['regedit.exe']):
                        continue
                elif os.path.realpath(os.path.join(machine / 'drive_c/windows' / directory, link)) == \
                        os.path.realpath(source):
                    checked_source()            # already linked to the installed Wine's own image
                    continue
                else:
                    raise ValueError('Refusing unexpected invalid regedit link')
            elif info is not None and stat.S_ISREG(info.st_mode) and valid_pe(fd, ['regedit.exe']):
                continue
            if info is not None and not legacy_link and (not stat.S_ISREG(info.st_mode) or info.st_size):
                raise ValueError('Nonempty invalid regedit image requires explicit recovery')
            checked_source()
            replacements.append((fd, source))
        # Validate both candidates before replacing either destination.
        for fd, source in replacements:
            machine_symlink(fd, 'regedit.exe', str(source))
    finally:
        for descriptor in directories.values():
            os.close(descriptor)


# ---------------------------------------------------------------------------
# UWP XAML support (execution policy uwp-xaml-extract-register-v1). Nothing third-party runs: the pinned archive is
# downloaded and verified, its members are checked and unpacked into the machine root's Program Files (published by
# rename), and one reg import names the directory to Wine's windows.ui.xaml.dll (HKLM\Software\Wine\Xaml
# ComponentPath) and registers the component's Windows.UI.Xaml classes to that library. The receipt lists every
# installed file with its digest and every registered class; uninstall removes exactly those.
# ---------------------------------------------------------------------------
UWP_XAML_POLICY = 'uwp-xaml-extract-register-v1'
UWP_XAML_ROOT = 'drive_c/Program Files/WineExplorer/Xaml'
UWP_XAML_WINDOWS_ROOT = 'C:\\Program Files\\WineExplorer\\Xaml'
UWP_XAML_DLL = 'C:\\windows\\system32\\windows.ui.xaml.dll'
UWP_XAML_CLASS = re.compile(r'^Windows\.UI\.Xaml(\.[A-Za-z_][A-Za-z0-9_]*)+$')
UWP_XAML_MEMBER = re.compile(r'^[A-Za-z0-9._+-]+(/[A-Za-z0-9._+-]+)*$')
UWP_XAML_REQUIRED = ('XamlShim.exe', 'XamlShim.dll', 'hostfxr.dll', 'classes.txt', 'LICENSE.txt', 'NOTICE.txt')
UWP_XAML_REDIRECTS = ('https://github.com/zcomputerwiz/uno-uwp/releases/download/',
                      'https://release-assets.githubusercontent.com/')
UWP_XAML_MAX_MEMBERS = 2000
UWP_XAML_MAX_BYTES = 512 * 1024 * 1024
UWP_XAML_MAX_CLASSES = 5000
UWP_XAML_OWNER = 0   # owner of the directories it publishes into (tests run unprivileged)


def uwp_xaml_payload(verb):
    payloads = COMPONENTS_METADATA[verb]['payloads']
    if len(payloads) != 1 or payloads[0]['architecture'] != 'x64':
        raise ValueError('UWP XAML: exactly one x64 payload is supported')
    return payloads[0]


def cache_uwp_xaml_payload(verb, cache):
    """The pinned archive in the root-owned cache; downloaded only after the terms were accepted."""
    return cache_pinned_download(verb, uwp_xaml_payload(verb), cache, UWP_XAML_REDIRECTS, 'UWP XAML')


def cache_pinned_download(verb, payload, cache, redirects, label, deadline=600):
    """PAYLOAD ({name, url, size, sha256}) in the root-owned cache directory of VERB, downloaded over https from the
    approved hosts REDIRECTS within DEADLINE seconds when it is not there yet, and checked against its size and hash
    either way."""
    destination = cache / verb
    if destination.is_symlink():
        raise ValueError(f'Refusing symlinked {label} cache')
    destination.mkdir(mode=0o700, exist_ok=True)
    if destination.stat().st_uid != 0 or destination.stat().st_mode & 0o022:
        raise ValueError(f'{label} cache must be root-owned and private')
    target = destination / payload['name']
    if target.exists() or target.is_symlink():
        if (target.is_symlink() or not target.is_file() or target.stat().st_uid != 0 or
                target.stat().st_mode & 0o022):
            raise ValueError(f'Untrusted cached {label} payload')
        if target.stat().st_size == payload['size'] and file_sha256(target) == payload['sha256']:
            return target
        raise ValueError(f'Corrupt {label} cache; nothing installed')
    if not payload['url'].startswith(redirects):
        raise ValueError(f'Unapproved {label} download location')
    temporary = None
    try:
        with urllib.request.urlopen(payload['url'], timeout=20) as response:
            if not response.url.startswith(redirects):
                raise ValueError(f'Unexpected {label} download redirect')
            with tempfile.NamedTemporaryFile(dir=destination, prefix='.download-', delete=False) as out:
                temporary = Path(out.name)
                total, digest = 0, hashlib.sha256()
                deadline = time.monotonic() + deadline
                while True:
                    chunk = response.read(65536)
                    if not chunk:
                        break
                    total += len(chunk)
                    if total > payload['size'] or time.monotonic() > deadline:
                        raise ValueError(f'{label} download exceeded approved size/deadline')
                    digest.update(chunk)
                    out.write(chunk)
                out.flush()
                os.fsync(out.fileno())
        if total != payload['size'] or digest.hexdigest() != payload['sha256']:
            raise ValueError(f'{label} payload hash/size mismatch; nothing installed')
        os.chmod(temporary, 0o600)
        os.replace(temporary, target)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return target


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as source:
        while chunk := source.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


# The machine root's layout (setup-machine-root.sh): root:<machine group> (wexsys, or WEX_MACHINE_GROUP), directories
# 2775 with a default ACL g:<group>:rwX, files 664 (775 for .exe and .dll). No desktop user is in that group; machine
# writes go through session 0 / wex-sysrun. What the components add there follows the same layout.
MACHINE_DIR_MODE = 0o2775


def machine_group_gid():
    """The gid of the configured machine group; ValueError when it does not exist."""
    name = os.environ.get('WEX_MACHINE_GROUP', 'wexsys')
    try:
        return grp.getgrnam(name).gr_gid
    except KeyError:
        raise ValueError(f'Machine group {name} does not exist') from None


def machine_file_mode(path):
    return 0o775 if path.suffix.lower() in ('.exe', '.dll') else 0o664


# The machine root is writable by the machine group, whose members (a per-machine installer started through wex-sysrun, a
# session 0 Wine process) can swap names in it while root works. Root therefore never acts on the machine root by path name:
# every directory is opened from its parent without following links and checked by fstat, files are opened without following
# links and changed through their descriptor, and a tree is built in a root-only area beside the machine root (STAGE_AREA,
# 0700) and published by one rename between a checked directory descriptor and the staging area.
DIR_OPEN_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
FILE_OPEN_FLAGS = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC    # no wait on a FIFO planted in a name
STAGE_AREA_NAME = '.uwp-stage'
MAX_MACHINE_FILE_BYTES = 1024 * 1024


def machine_open_dir(parent_fd, name, admin_gid, create=False):
    """A directory of the machine root as a descriptor, opened from its parent without following a link and checked: root
    owns it, others cannot write it, and group write is the machine group's alone. CREATE makes it (2775, machine group)."""
    try:
        fd = os.open(name, DIR_OPEN_FLAGS, dir_fd=parent_fd)
    except FileNotFoundError:
        if not create:
            raise
        os.mkdir(name, 0o700, dir_fd=parent_fd)
        fd = os.open(name, DIR_OPEN_FLAGS, dir_fd=parent_fd)
        try:
            os.fchown(fd, UWP_XAML_OWNER, admin_gid)
            os.fchmod(fd, MACHINE_DIR_MODE)
        except BaseException:
            os.close(fd)
            raise
    except OSError as error:
        raise ValueError(f'Refusing {name}: {error.strerror} (a link or not a directory)') from None
    try:
        info = os.fstat(fd)
        group_ok = not info.st_mode & 0o020 or info.st_gid == admin_gid
        if info.st_uid != UWP_XAML_OWNER or info.st_mode & 0o002 or not group_ok:
            raise ValueError(f'Untrusted directory: {name}')
    except BaseException:
        os.close(fd)
        raise
    return fd


def machine_open_path(machine, existing, admin_gid, create=()):
    """The descriptor of machine/EXISTING.../CREATE...: each step opened from the one before (the EXISTING ones must be
    there, the CREATE ones are made). The caller closes it."""
    try:
        fd = os.open(machine, DIR_OPEN_FLAGS)
    except OSError as error:
        raise ValueError(f'Refusing the machine root: {error.strerror}') from None
    try:
        info = os.fstat(fd)
        if info.st_uid != UWP_XAML_OWNER or info.st_mode & 0o002 or info.st_gid != admin_gid:
            raise ValueError(f'Untrusted machine root: {machine}')
        for names, make in ((existing, False), (create, True)):
            for name in names:
                child = machine_open_dir(fd, name, admin_gid, make)
                os.close(fd)
                fd = child
        return fd
    except BaseException:
        os.close(fd)
        raise


def machine_open_regular(dir_fd, parts):
    """A regular file below a directory descriptor (PARTS: the names of the path), opened without following a link and
    without waiting for a FIFO; the caller closes it."""
    opened = []
    try:
        current = dir_fd
        for name in parts[:-1]:
            current = os.open(name, DIR_OPEN_FLAGS, dir_fd=current)
            opened.append(current)
        fd = os.open(parts[-1], FILE_OPEN_FLAGS, dir_fd=current)
    except OSError as error:
        raise ValueError(f'Cannot open {"/".join(parts)}: {error.strerror}') from None
    finally:
        for descriptor in opened:
            os.close(descriptor)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise ValueError(f'Not a regular file: {"/".join(parts)}')
    except BaseException:
        os.close(fd)
        raise
    return fd


def machine_read_regular(dir_fd, parts, limit=MAX_MACHINE_FILE_BYTES):
    """The bytes of a regular file below a directory descriptor, read once (at most LIMIT)."""
    fd = machine_open_regular(dir_fd, parts)
    try:
        if os.fstat(fd).st_size > limit:
            raise ValueError(f'Oversized file: {"/".join(parts)}')
        with os.fdopen(fd, 'rb', closefd=False) as source:
            return source.read(limit + 1)
    finally:
        os.close(fd)


def machine_stage_area(machine):
    """The root-only staging area beside the machine root (made 0700 when missing); a Path."""
    parent = machine.parent
    info = parent.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != UWP_XAML_OWNER or info.st_mode & 0o022:
        raise ValueError(f'Untrusted directory: {parent}')
    area = parent / STAGE_AREA_NAME
    try:
        area.mkdir(mode=0o700)
    except FileExistsError:
        pass
    info = area.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != UWP_XAML_OWNER or info.st_mode & 0o077:
        raise ValueError(f'Untrusted staging area: {area}')
    return area


def machine_stage_finalize(tree, admin_gid):
    """Owner, group and modes of a staged tree, which only root can reach: directories 2775, files 664 (775 for .exe and
    .dll), all root and the machine group. Anything that is not a plain file or directory is refused."""
    entries = [tree]
    for directory, directories, files in os.walk(tree):
        entries.extend(os.path.join(directory, name) for name in directories + files)
    for entry in entries:
        info = os.lstat(entry)
        if stat.S_ISDIR(info.st_mode):
            mode = MACHINE_DIR_MODE
        elif stat.S_ISREG(info.st_mode):
            mode = machine_file_mode(Path(entry))
        else:
            raise ValueError(f'Unexpected entry in a staged tree: {entry}')
        os.lchown(entry, UWP_XAML_OWNER, admin_gid)
        os.chmod(entry, mode)


def remove_staged(path):
    """Remove a name of the staging area: a tree, a file or a link (a link is removed, not followed)."""
    path = Path(path)
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def machine_publish(tree, parent_fd, name, area):
    """Move the staged TREE to PARENT_FD/NAME; whatever is there goes to the staging area first (and is removed)."""
    trash = area / ('.old-' + uuid.uuid4().hex)
    try:
        try:
            os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
            os.rename(name, trash, src_dir_fd=parent_fd)
        except FileNotFoundError:
            trash = None
        os.rename(tree, name, dst_dir_fd=parent_fd)
    except OSError as error:
        if error.errno == errno.EXDEV:
            raise ValueError('The machine root and its staging area must be on one file system') from None
        raise
    if trash is not None:
        remove_staged(trash)


def machine_remove(parent_fd, name, area):
    """Remove an entry of a machine root directory: it is moved to the staging area by one rename, then removed there."""
    trash = area / ('.removed-' + uuid.uuid4().hex)
    try:
        os.rename(name, trash, src_dir_fd=parent_fd)
    except FileNotFoundError:
        return
    except OSError as error:
        if error.errno == errno.EXDEV:
            raise ValueError('The machine root and its staging area must be on one file system') from None
        raise
    remove_staged(trash)


def machine_write_file(dir_fd, name, data, admin_gid, mode=0o664, owner=None):
    """A file of a machine root directory, written to a temporary name and renamed over NAME, all by descriptor.
    OWNER: root's (UWP_XAML_OWNER) unless another account is named."""
    temporary = '.record-' + uuid.uuid4().hex
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=dir_fd)
    try:
        with os.fdopen(fd, 'wb', closefd=False) as out:
            os.fchown(fd, UWP_XAML_OWNER if owner is None else owner, admin_gid)
            os.fchmod(fd, mode)
            out.write(data)
            out.flush()
            os.fsync(fd)
        os.rename(temporary, name, src_dir_fd=dir_fd, dst_dir_fd=dir_fd)
    except BaseException:
        try:
            os.unlink(temporary, dir_fd=dir_fd)
        except OSError:
            pass
        raise
    finally:
        os.close(fd)


def machine_symlink(dir_fd, name, target):
    """A link of a machine root directory, made under a temporary name and renamed over NAME, all by descriptor."""
    temporary = '.link-' + uuid.uuid4().hex
    os.symlink(target, temporary, dir_fd=dir_fd)
    try:
        os.rename(temporary, name, src_dir_fd=dir_fd, dst_dir_fd=dir_fd)
    except BaseException:
        try:
            os.unlink(temporary, dir_fd=dir_fd)
        except OSError:
            pass
        raise


MAX_MACHINE_HASH_BYTES = 512 * 1024 * 1024     # a library or font hashed or copied in place
MAX_MACHINE_HIVE_BYTES = 256 * 1024 * 1024     # system.reg


def machine_open_for_reading(machine, parts=()):
    """A directory of the machine root (PARTS: the names below it) as a descriptor to read through: each step opened
    from the one before without following a link. There is no ownership check, because verification also runs for a
    desktop user (is_installed) and only reads; anything written goes through machine_open_path. The caller closes it."""
    try:
        fd = os.open(machine, DIR_OPEN_FLAGS)
    except OSError as error:
        raise ValueError(f'Cannot open the machine root: {error.strerror}') from None
    try:
        for name in parts:
            child = os.open(name, DIR_OPEN_FLAGS, dir_fd=fd)
            os.close(fd)
            fd = child
    except OSError as error:
        os.close(fd)
        raise ValueError(f'Cannot open {"/".join(parts)}: {error.strerror} (a link or not a directory)') from None
    return fd


def machine_fd_sha256(fd, limit=MAX_MACHINE_HASH_BYTES):
    """The SHA-256 of an open regular file from its start; more than LIMIT bytes are refused."""
    digest, total = hashlib.sha256(), 0
    os.lseek(fd, 0, os.SEEK_SET)
    while chunk := os.read(fd, 1 << 20):
        total += len(chunk)
        if total > limit:
            raise ValueError('Oversized file')
        digest.update(chunk)
    return digest.hexdigest()


def installed_wine_file(path, limit=MAX_MACHINE_HASH_BYTES, read=True):
    """(bytes, fstat) of a file of the installed Wine: every directory above it up to / is root's, a real directory and
    not writable by group or others, and so is the file, which is opened without following a link or waiting for a
    FIFO. Nobody but root can then change what is read. READ false: only the checks (bytes None)."""
    for entry in path.parents:
        info = os.lstat(entry)
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError(f'Untrusted installed Wine file: {path}')
    try:
        fd = os.open(path, FILE_OPEN_FLAGS)
    except FileNotFoundError:
        raise
    except OSError as error:
        raise ValueError(f'Untrusted installed Wine file: {path} ({error.strerror})') from None
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError(f'Untrusted installed Wine file: {path}')
        if info.st_size > limit:
            raise ValueError(f'Oversized installed Wine file: {path}')
        if not read:
            return None, info
        with os.fdopen(fd, 'rb', closefd=False) as source:
            data = source.read(limit + 1)
        if len(data) > limit:
            raise ValueError(f'Oversized installed Wine file: {path}')
        return data, info
    finally:
        os.close(fd)


def pe_machine_is(data, expected):
    """DATA is a PE image for the machine EXPECTED (0x14c, 0x8664)."""
    pe = struct.unpack_from('<I', data, 0x3c)[0] if len(data) >= 64 else len(data)
    return (data[:2] == b'MZ' and pe + 6 <= len(data) and data[pe:pe + 4] == b'PE\0\0'
            and struct.unpack_from('<H', data, pe + 4)[0] == expected)


@contextmanager
def _components_lock(machine):
    """The lock that installs and uninstalls hold (a non-blocking one: a run that finds it taken reports and stops)."""
    if machine.is_symlink() or machine.stat().st_uid != UWP_XAML_OWNER:
        raise ValueError('Machine prefix must be a root-owned real directory')
    descriptor = os.open(machine.parent / '.components.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def machine_admin_gid(machine):
    """The machine group, when the machine root is a root-owned real directory of that group that others cannot
    write. A machine root of another group (root:users, root:sudo: not migrated, or made by hand) is refused: group
    write there would mean that desktop users may write."""
    info = machine.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != UWP_XAML_OWNER or info.st_mode & 0o002:
        raise ValueError(f'Untrusted machine root: {machine}')
    expected = machine_group_gid()
    if info.st_gid != expected:
        raise ValueError(f'Machine root {machine} does not belong to the machine group')
    return expected


def uwp_xaml_unpack(archive, machine, arch):
    """Check every member and unpack into <root>/<arch>: staged in the root-only area, published by one rename.
    Returns ({member: [size, sha256]}, [classes])."""
    gid = machine_admin_gid(machine)
    area = machine_stage_area(machine)
    stage = area / ('xaml-' + uuid.uuid4().hex)
    stage.mkdir(mode=0o700)
    tree = stage / 'tree'
    tree.mkdir(mode=0o700)
    files = {}
    try:
        with zipfile.ZipFile(archive) as package:
            members = package.infolist()
            if len(members) > UWP_XAML_MAX_MEMBERS or sum(m.file_size for m in members) > UWP_XAML_MAX_BYTES:
                raise ValueError('UWP XAML archive exceeds its member limits')
            for member in members:
                name = member.filename
                if not name.startswith('XamlShim/'):
                    raise ValueError('UWP XAML archive member outside XamlShim/: ' + name)
                relative = name[len('XamlShim/'):]
                if member.is_dir() or not relative:
                    continue
                if (not UWP_XAML_MEMBER.match(relative) or any(part in ('.', '..') for part in relative.split('/')) or
                        (member.external_attr >> 16) & 0o170000 == 0o120000):
                    raise ValueError('Unsafe UWP XAML archive member: ' + name)
                target = tree / relative
                target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                digest = hashlib.sha256()
                size = 0
                with package.open(member) as source, open(target, 'xb') as out:
                    while chunk := source.read(1 << 20):
                        size += len(chunk)
                        if size > member.file_size:
                            raise ValueError('UWP XAML archive member larger than declared: ' + name)
                        digest.update(chunk)
                        out.write(chunk)
                    out.flush()
                    os.fsync(out.fileno())
                files[relative] = [size, digest.hexdigest()]
        for required in UWP_XAML_REQUIRED:
            if required not in files:
                raise ValueError('UWP XAML archive lacks ' + required)
        classes = (tree / 'classes.txt').read_text(encoding='utf-8').split()
        if not classes or len(classes) > UWP_XAML_MAX_CLASSES or any(not UWP_XAML_CLASS.match(c) for c in classes):
            raise ValueError('UWP XAML archive has an invalid class list')
        machine_stage_finalize(tree, gid)
        directory = machine_open_path(machine, ('drive_c', 'Program Files'), gid, ('WineExplorer', 'Xaml'))
        try:
            machine_publish(tree, directory, arch, area)
        finally:
            os.close(directory)
        return files, classes
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def uwp_xaml_registry_text(arch, classes):
    lines = ['Windows Registry Editor Version 5.00', '',
             '[HKEY_LOCAL_MACHINE\\Software\\Wine\\Xaml]',
             '"ComponentPath"="' + (UWP_XAML_WINDOWS_ROOT + '\\' + arch).replace('\\', '\\\\') + '"', '']
    for name in classes:
        lines += ['[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\' + name + ']',
                  '"DllPath"="' + UWP_XAML_DLL.replace('\\', '\\\\') + '"',
                  '"ActivationType"=dword:00000000',
                  '"Threading"=dword:00000000', '']
    return '\r\n'.join(lines) + '\r\n'


def read_machine_hive(machine):
    """Wine's system.reg as {key: [value lines]}, keys as written there (backslashes doubled), read without Wine."""
    try:
        root = machine_open_for_reading(machine)
        try:
            raw = machine_read_regular(root, ['system.reg'], MAX_MACHINE_HIVE_BYTES)
        finally:
            os.close(root)
    except ValueError:
        return {}                       # missing, a link, a FIFO or oversized: nothing registered
    keys, current = {}, None
    for line in raw.decode('utf-8', errors='replace').splitlines():
        if line.startswith('['):
            end = line.find(']')
            current = keys.setdefault(line[1:end], []) if end > 0 else None
        elif current is not None and line.startswith('"'):
            current.append(line)
    return keys


def uwp_xaml_hive_has(machine, arch, classes):
    """The machine hive holds ComponentPath and every class registered to windows.ui.xaml.dll."""
    keys = read_machine_hive(machine)
    double = lambda s: s.replace('\\', '\\\\')
    if '"ComponentPath"="' + double(UWP_XAML_WINDOWS_ROOT + '\\' + arch) + '"' not in keys.get(double('Software\\Wine\\Xaml'), []):
        return False
    dll = '"DllPath"="' + double(UWP_XAML_DLL) + '"'
    return all(dll in keys.get(double('Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\' + name), [])
               for name in classes)


def verify_uwp_xaml(machine, data, full=False):
    """Files of the receipt present with their sizes (digests too when full) and the hive entries present."""
    arch = data.get('payload_architecture')
    files, classes = data.get('files'), data.get('classes')
    if arch != 'x64' or not isinstance(files, dict) or not isinstance(classes, list):
        raise ValueError('Malformed UWP XAML receipt')
    try:
        root = machine_open_for_reading(machine, tuple(UWP_XAML_ROOT.split('/')) + (arch,))
    except ValueError:
        raise ValueError('UWP XAML directory missing') from None
    try:
        for relative, (size, digest) in files.items():
            try:
                fd = machine_open_regular(root, relative.split('/'))
            except ValueError:
                raise ValueError('UWP XAML file missing or changed: ' + relative) from None
            try:
                if os.fstat(fd).st_size != size:
                    raise ValueError('UWP XAML file missing or changed: ' + relative)
                if (full or relative in ('XamlShim.dll', 'classes.txt')) and machine_fd_sha256(fd) != digest:
                    raise ValueError('UWP XAML file changed: ' + relative)
            finally:
                os.close(fd)
    finally:
        os.close(root)
    if not uwp_xaml_hive_has(machine, arch, classes):
        raise ValueError('UWP XAML registration missing')


def uwp_xaml_unregister(wine, classes):
    """Delete the class keys and ComponentPath (and the Xaml key when nothing else is in it); missing keys are fine."""
    uwp_xaml_unregister_classes(wine, classes)
    wine.reg('delete', 'HKLM\\Software\\Wine\\Xaml', '/v', 'ComponentPath', '/f')
    query = wine.reg('query', 'HKLM\\Software\\Wine\\Xaml')
    if query.returncode == 0 and 'REG_' not in query.stdout.decode('utf-8', 'replace'):
        wine.reg('delete', 'HKLM\\Software\\Wine\\Xaml', '/f')
    wine.wait_idle()


def uwp_xaml_previous_classes(receipt_dir, verb):
    """The classes an earlier installation's receipt lists (an upgrade drops the ones the new version lacks)."""
    receipt = receipt_dir / (verb + '.json')
    if receipt.is_symlink() or not receipt.is_file() or receipt.stat().st_uid != UWP_XAML_OWNER:
        return []
    try:
        classes = json.loads(receipt.read_text(encoding='utf-8')).get('classes')
    except (OSError, ValueError, AttributeError):
        return []
    if not isinstance(classes, list) or any(not isinstance(c, str) or not UWP_XAML_CLASS.match(c) for c in classes):
        return []
    return classes


def uwp_xaml_unregister_classes(wine, classes):
    """Delete only these class keys (ComponentPath stays); missing keys are fine."""
    for name in classes:
        wine.reg('delete', 'HKLM\\Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\' + name, '/f')


def uwp_xaml_drop_receipt(receipt_dir, verb):
    """After a failed upgrade rolled the machine back to nothing, no receipt may claim the earlier version."""
    for name in (verb + '.json', verb + '.sha256'):
        path = receipt_dir / name
        if path.is_symlink() or path.is_file():
            path.unlink()


def uwp_xaml_remove_directory(machine, arch):
    gid = machine_admin_gid(machine)
    try:
        directory = machine_open_path(machine, ('drive_c', 'Program Files', 'WineExplorer', 'Xaml'), gid)
    except FileNotFoundError:
        return
    try:
        machine_remove(directory, arch, machine_stage_area(machine))
    finally:
        os.close(directory)


def uwp_xaml_rollback(machine, arch, classes, wine):
    """After a failed registration or verification: best effort, never masking the original error."""
    try:
        uwp_xaml_unregister(wine, classes)
    except Exception as error:
        print(f'UWP XAML rollback: registry cleanup failed: {error}', file=sys.stderr)
    try:
        uwp_xaml_remove_directory(machine, arch)
    except Exception as error:
        print(f'UWP XAML rollback: directory removal failed: {error}', file=sys.stderr)


@contextmanager
def _machine_maintenance():
    """The components lock, no pending recovery or interrupted OOBE operation, and Wine for the machine root
    (MachineWine: through session 0 when there is one, else root's own server); yields the MachineWine. The caller
    calls its require_idle() once it knows it has work to do; what its programs changed is saved when this ends."""
    if MACHINE.is_symlink() or MACHINE.stat().st_uid != 0:
        raise ValueError('Machine prefix must be a root-owned real directory')
    lock_path = MACHINE.parent / '.components.lock'
    descriptor = os.open(lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if OPERATION_STATE.exists() or OPERATION_STATE.is_symlink():
            checked_operation_directory()
            if any(record['status'] == 'UNKNOWN' for record in read_operation_journal().values()):
                raise ValueError('Interrupted OOBE installation requires administrator reconciliation before install later')
        recovery_marker = MACHINE.parent / 'component-recovery-required'
        if recovery_marker.exists() or recovery_marker.is_symlink():
            raise ValueError('Machine prefix requires administrator recovery before another installation')
        with MachineWine(MACHINE) as wine:
            yield wine
            wine.wait_idle()


def _uwp_xaml_receipt_dir():
    receipt = MACHINE.parent / 'component-receipts'
    receipt.mkdir(mode=0o755, exist_ok=True)
    if receipt.is_symlink() or receipt.stat().st_uid != 0 or receipt.stat().st_mode & 0o022:
        raise ValueError('Untrusted component receipt directory')
    receipt.chmod(0o755)
    return receipt


def install_uwp_xaml(verb, accepted_digest):
    meta = COMPONENTS_METADATA[verb]
    _, current = license_text(verb)
    if current != accepted_digest:
        raise ValueError('Terms changed; review and accept the current text')
    with _machine_maintenance() as wine:
        if is_installed(verb, accepted_digest, machine=MACHINE):
            return
        wine.require_idle()
        if CACHE.is_symlink():
            raise ValueError('Refusing symlinked component cache')
        CACHE.mkdir(mode=0o700, exist_ok=True)
        if CACHE.stat().st_uid != 0 or CACHE.stat().st_mode & 0o022:
            raise ValueError('Component cache must be root-owned and not writable by others')
        payload = uwp_xaml_payload(verb)
        archive = cache_uwp_xaml_payload(verb, CACHE)
        receipt_dir = MACHINE.parent / 'component-receipts'
        previous = uwp_xaml_previous_classes(receipt_dir, verb)
        files, classes = uwp_xaml_unpack(archive, MACHINE, payload['architecture'])
        data = {
            'verb': verb,
            'title': meta.get('title', verb),
            'sha256': current,
            'terms_sha256': current,
            'execution_policy': UWP_XAML_POLICY,
            'component_identity': component_identity(verb),
            'payload_sha256': payload['sha256'],
            'payload_architecture': payload['architecture'],
            'files': files,
            'classes': classes,
            'installed_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        }
        try:
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='', dir=CACHE, prefix='.uwp-xaml-',
                                             suffix='.reg', delete=False) as out:
                registry = Path(out.name)
                out.write(uwp_xaml_registry_text(payload['architecture'], classes))
            try:
                wine.import_file(registry)
            finally:
                registry.unlink(missing_ok=True)
            wine.wait_idle()
            verify_uwp_xaml(MACHINE, data, full=True)
        except BaseException:
            # no receipt will name these files: take back what this attempt added before reporting the failure; the
            # unpack already replaced an earlier version, so its classes and receipt go too
            uwp_xaml_rollback(MACHINE, payload['architecture'], sorted(set(classes) | set(previous)), wine)
            try:
                uwp_xaml_drop_receipt(receipt_dir, verb)
            except OSError as error:
                print(f'UWP XAML rollback: receipt removal failed: {error}', file=sys.stderr)
            raise
        # an upgrade: classes the earlier version served and this one does not must not stay registered to it
        stale = [name for name in previous if name not in classes]
        if stale:
            try:
                uwp_xaml_unregister_classes(wine, stale)
                wine.wait_idle()
            except Exception as error:
                print(f'UWP XAML upgrade: removing {len(stale)} classes failed: {error}', file=sys.stderr)
        receipt = _uwp_xaml_receipt_dir()
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=receipt, prefix='.' + verb, delete=False) as out:
            temporary_receipt = Path(out.name)
            os.fchmod(out.fileno(), 0o644)
            json.dump(data, out, indent=2)
            out.write('\n')
            out.flush()
            os.fsync(out.fileno())
        try:
            os.replace(temporary_receipt, receipt / (verb + '.json'))
        finally:
            temporary_receipt.unlink(missing_ok=True)
        (receipt / (verb + '.sha256')).write_text(current + '\n', encoding='ascii')


# ---------------------------------------------------------------------------
# PowerShell 7 (execution policy powershell-extract-v1). Nothing runs and Wine is not started: the pinned release zip
# (Microsoft's open-source PowerShell, MIT) is downloaded after the terms were accepted, its members are checked and
# unpacked into the machine root's Program Files\PowerShell\7 (published by rename), together with the WexComputer
# module, which the image carries (Add-Computer and its relatives, which PowerShell 7 lacks) and which goes to
# <install>\Modules\WexComputer, where PowerShell finds it without Import-Module. The receipt lists every installed
# file with its digest; uninstall removes the directory. No registry entries.
# ---------------------------------------------------------------------------
POWERSHELL_POLICY = 'powershell-extract-v1'
POWERSHELL_PARENT = ('drive_c', 'Program Files', 'PowerShell')
POWERSHELL_LEAF = '7'
POWERSHELL_WINDOWS_ROOT = 'C:\\Program Files\\PowerShell\\7'
POWERSHELL_MEMBER = re.compile(r'^[A-Za-z0-9._+-]+(/[A-Za-z0-9._+-]+)*$')
POWERSHELL_REQUIRED = ('pwsh.exe', 'pwsh.dll', 'System.Management.Automation.dll', 'Microsoft.PowerShell.ConsoleHost.dll',
                       'pwsh.runtimeconfig.json', 'LICENSE.txt', 'ThirdPartyNotices.txt',
                       'Modules/CimCmdlets/CimCmdlets.psd1')
POWERSHELL_REDIRECTS = ('https://github.com/PowerShell/PowerShell/releases/download/',
                        'https://release-assets.githubusercontent.com/')
POWERSHELL_MAX_MEMBERS = 2000
POWERSHELL_MAX_BYTES = 512 * 1024 * 1024
# the WexComputer module (src/powershell/WexComputer in this repository), as the image installs it
POWERSHELL_MODULE_SOURCE = Path('/usr/local/share/wineexplorer/powershell/WexComputer')
POWERSHELL_MODULE_FILES = ('WexComputer.psd1', 'WexComputer.psm1')
POWERSHELL_MODULE_LIMIT = 1024 * 1024


def powershell_payload(verb):
    payloads = COMPONENTS_METADATA[verb]['payloads']
    if len(payloads) != 1 or payloads[0]['architecture'] != 'x64':
        raise ValueError('PowerShell: exactly one x64 payload is supported')
    return payloads[0]


def cache_powershell_payload(verb, cache):
    """The pinned release zip in the root-owned cache; downloaded only after the terms were accepted."""
    return cache_pinned_download(verb, powershell_payload(verb), cache, POWERSHELL_REDIRECTS, 'PowerShell')


def powershell_module_files(source=None):
    """{relative name under Modules/WexComputer: bytes} of the module the image carries, read without following links
    from a root-owned directory; empty when the image has no module (PowerShell then installs without it)."""
    source = POWERSHELL_MODULE_SOURCE if source is None else source
    try:
        info = source.lstat()
    except FileNotFoundError:
        return {}
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != UWP_XAML_OWNER or info.st_mode & 0o022:
        raise ValueError(f'Untrusted WexComputer module directory: {source}')
    files = {}
    for name in POWERSHELL_MODULE_FILES:
        data, _ = installed_wine_file(source / name, POWERSHELL_MODULE_LIMIT)
        files[name] = data
    return files


def powershell_unpack(archive, machine, module=None):
    """Check every member of the release zip and unpack into Program Files\\PowerShell\\7, with the WexComputer module
    added under Modules/: staged in the root-only area, published by one rename.
    Returns {member: [size, sha256]} for every file."""
    gid = machine_admin_gid(machine)
    area = machine_stage_area(machine)
    stage = area / ('pwsh-' + uuid.uuid4().hex)
    stage.mkdir(mode=0o700)
    tree = stage / 'tree'
    tree.mkdir(mode=0o700)
    files = {}
    folded = set()
    try:
        with zipfile.ZipFile(archive) as package:
            members = package.infolist()
            if len(members) > POWERSHELL_MAX_MEMBERS or sum(m.file_size for m in members) > POWERSHELL_MAX_BYTES:
                raise ValueError('PowerShell archive exceeds its member limits')
            for member in members:
                relative = member.filename
                if member.is_dir():
                    continue
                if (not POWERSHELL_MEMBER.match(relative) or any(part in ('.', '..') for part in relative.split('/')) or
                        (member.external_attr >> 16) & 0o170000 == 0o120000 or
                        relative.casefold() in folded or relative.casefold().startswith('modules/wexcomputer/')):
                    raise ValueError('Unsafe PowerShell archive member: ' + relative)
                folded.add(relative.casefold())   # Windows names do not tell A.dll from a.dll
                target = tree / relative
                target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                digest = hashlib.sha256()
                size = 0
                with package.open(member) as source, open(target, 'xb') as out:
                    while chunk := source.read(1 << 20):
                        size += len(chunk)
                        if size > member.file_size:
                            raise ValueError('PowerShell archive member larger than declared: ' + relative)
                        digest.update(chunk)
                        out.write(chunk)
                    out.flush()
                    os.fsync(out.fileno())
                files[relative] = [size, digest.hexdigest()]
        for required in POWERSHELL_REQUIRED:
            if required not in files:
                raise ValueError('PowerShell archive lacks ' + required)
        for name, data in (powershell_module_files() if module is None else module).items():
            relative = 'Modules/WexComputer/' + name
            target = tree / relative
            target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            with open(target, 'xb') as out:
                out.write(data)
                out.flush()
                os.fsync(out.fileno())
            files[relative] = [len(data), hashlib.sha256(data).hexdigest()]
        machine_stage_finalize(tree, gid)
        directory = machine_open_path(machine, ('drive_c', 'Program Files'), gid, ('PowerShell',))
        try:
            machine_publish(tree, directory, POWERSHELL_LEAF, area)
        finally:
            os.close(directory)
        return files
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def verify_powershell(machine, data, full=False):
    """Files of the receipt present with their sizes (digests too when full)."""
    files = data.get('files')
    if data.get('payload_architecture') != 'x64' or not isinstance(files, dict):
        raise ValueError('Malformed PowerShell receipt')
    if any(required not in files for required in POWERSHELL_REQUIRED):
        raise ValueError('PowerShell receipt lacks a required file')
    try:
        root = machine_open_for_reading(machine, POWERSHELL_PARENT + (POWERSHELL_LEAF,))
    except ValueError:
        raise ValueError('PowerShell directory missing') from None
    try:
        for relative, (size, digest) in files.items():
            try:
                fd = machine_open_regular(root, relative.split('/'))
            except ValueError:
                raise ValueError('PowerShell file missing or changed: ' + relative) from None
            try:
                if os.fstat(fd).st_size != size:
                    raise ValueError('PowerShell file missing or changed: ' + relative)
                if (full or relative in ('pwsh.exe', 'pwsh.dll', 'System.Management.Automation.dll') or
                        relative.startswith('Modules/WexComputer/')) and machine_fd_sha256(fd) != digest:
                    raise ValueError('PowerShell file changed: ' + relative)
            finally:
                os.close(fd)
    finally:
        os.close(root)


def powershell_remove_directory(machine):
    gid = machine_admin_gid(machine)
    try:
        directory = machine_open_path(machine, POWERSHELL_PARENT, gid)
    except FileNotFoundError:
        return
    try:
        machine_remove(directory, POWERSHELL_LEAF, machine_stage_area(machine))
    finally:
        os.close(directory)


def install_powershell(verb, accepted_digest):
    meta = COMPONENTS_METADATA[verb]
    _, current = license_text(verb)
    if current != accepted_digest:
        raise ValueError('Terms changed; review and accept the current text')
    with _machine_maintenance() as wine:
        if is_installed(verb, accepted_digest, machine=MACHINE):
            return
        wine.require_idle()
        if CACHE.is_symlink():
            raise ValueError('Refusing symlinked component cache')
        CACHE.mkdir(mode=0o700, exist_ok=True)
        if CACHE.stat().st_uid != 0 or CACHE.stat().st_mode & 0o022:
            raise ValueError('Component cache must be root-owned and not writable by others')
        payload = powershell_payload(verb)
        archive = cache_powershell_payload(verb, CACHE)
        receipt_dir = MACHINE.parent / 'component-receipts'
        module = powershell_module_files()
        if not module:
            print('PowerShell: the image has no WexComputer module; installing PowerShell without it', file=sys.stderr)
        files = powershell_unpack(archive, MACHINE, module)
        data = {
            'verb': verb,
            'title': meta.get('title', verb),
            'sha256': current,
            'terms_sha256': current,
            'execution_policy': POWERSHELL_POLICY,
            'component_identity': component_identity(verb),
            'payload_sha256': payload['sha256'],
            'payload_architecture': payload['architecture'],
            'files': files,
            'module': bool(module),
            'installed_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        }
        try:
            verify_powershell(MACHINE, data, full=True)
        except BaseException:
            # no receipt will name these files: take back what this attempt added, and an earlier receipt, which the
            # unpack replaced the files of
            try:
                powershell_remove_directory(MACHINE)
            except Exception as error:
                print(f'PowerShell rollback: directory removal failed: {error}', file=sys.stderr)
            try:
                uwp_xaml_drop_receipt(receipt_dir, verb)
            except OSError as error:
                print(f'PowerShell rollback: receipt removal failed: {error}', file=sys.stderr)
            raise
        receipt = _uwp_xaml_receipt_dir()
        atomic_json(receipt / (verb + '.json'), data, mode=0o644)
        (receipt / (verb + '.sha256')).write_text(current + '\n', encoding='ascii')


def uninstall_powershell(verb):
    receipt = MACHINE.parent / 'component-receipts' / (verb + '.json')
    if receipt.is_symlink() or not receipt.is_file() or receipt.stat().st_uid != 0:
        raise ValueError(f'{verb} is not installed')
    powershell_remove_directory(MACHINE)
    receipt.unlink()
    (receipt.parent / (verb + '.sha256')).unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# UWP application frameworks (uwp-frameworks-extract-register-v1): Microsoft's framework packages (.appx) taken out of
# pinned containers (nuget.org packages, Visual Studio payload cabinets), unpacked for every user into
# C:\Program Files\WindowsApps\<PackageFullName> and published by a record in the machine's package catalog,
# C:\ProgramData\WineExplorer\appx-catalog (Wine kernelbase resolves the dependencies of a user's packages there; a
# record there counts only as a framework). No package code runs and Wine is not started.
# ---------------------------------------------------------------------------
UWP_FRAMEWORKS_POLICY = 'uwp-frameworks-extract-register-v1'
FRAMEWORK_STORE = 'drive_c/Program Files/WindowsApps'
FRAMEWORK_WINDOWS_STORE = 'C:\\Program Files\\WindowsApps'
FRAMEWORK_CATALOG = 'drive_c/ProgramData/WineExplorer/appx-catalog'
FRAMEWORK_REDIRECTS = ('https://api.nuget.org/v3-flatcontainer/',
                       'https://download.visualstudio.microsoft.com/download/pr/')
FRAMEWORK_MAX_MEMBERS = 10000
FRAMEWORK_MAX_BYTES = 256 * 1024 * 1024
FRAMEWORK_NAME = re.compile(r'^[A-Za-z0-9.-]{3,50}$')
FRAMEWORK_VERSION = re.compile(r'^\d{1,5}\.\d{1,5}\.\d{1,5}\.\d{1,5}$')
FRAMEWORK_ARCHITECTURES = ('x64', 'x86', 'arm64', 'neutral')
FRAMEWORK_BAD_CHARACTERS = re.compile(r'[\x00-\x1f<>:"|?*\\]')
PUBLISHER_ID_ALPHABET = '0123456789abcdefghjkmnpqrstvwxyz'


def appx_publisher_id(publisher):
    """The 13-character publisher ID of a package family: the first 64 bits of the SHA-256 of the UTF-16LE
    publisher, padded with one zero bit to 65 bits, in Crockford's base32 (lower case)."""
    bits = int.from_bytes(hashlib.sha256(publisher.encode('utf-16-le')).digest()[:8], 'big') << 1
    return ''.join(PUBLISHER_ID_ALPHABET[(bits >> (5 * (12 - i))) & 31] for i in range(13))


def appx_full_name(name, version, architecture, publisher):
    return f'{name}_{version}_{architecture}__{appx_publisher_id(publisher)}'


def appx_identity(appx):
    """(name, version, architecture, publisher, is_framework) from the AppxManifest.xml of the package bytes."""
    with zipfile.ZipFile(io.BytesIO(appx)) as package:
        info = package.getinfo('AppxManifest.xml')
        if info.file_size > 1024 * 1024:
            raise ValueError('Oversized package manifest')
        root = xml_fromstring(package.read(info))
    identity = framework = None
    for element in root.iter():
        tag = element.tag.rsplit('}', 1)[-1]
        if tag == 'Identity' and identity is None:
            identity = element.attrib
        elif tag == 'Framework' and element.text is not None:
            framework = element.text.strip().lower() == 'true'
    if identity is None:
        raise ValueError('Package manifest without an identity')
    return (identity.get('Name', ''), identity.get('Version', ''), identity.get('ProcessorArchitecture', 'neutral'),
            identity.get('Publisher', ''), bool(framework))


def appx_member_path(raw):
    """The relative path of a package member: '/' separated, percent-encoded where the packaging rules require it
    (an encoded separator is not a separator); None when it is not a safe relative path."""
    parts = []
    for part in raw.split('/'):
        if re.search(r'%(?![0-9A-Fa-f]{2})', part):
            return None
        try:
            decoded = urllib.parse.unquote(part, encoding='utf-8', errors='strict')
        except UnicodeDecodeError:
            return None
        if (not decoded or decoded in ('.', '..') or '/' in decoded or FRAMEWORK_BAD_CHARACTERS.search(decoded) or
                decoded.endswith(('.', ' '))):
            return None
        parts.append(decoded)
    return '/'.join(parts)


def uwp_frameworks_packages(verb):
    """[(container, package)] of the component, every pin checked for shape."""
    meta = COMPONENTS_METADATA[verb]
    result = []
    for container in meta['containers']:
        if container['format'] not in ('zip', 'cab') or not container['url'].startswith(FRAMEWORK_REDIRECTS):
            raise ValueError('Unapproved framework container')
        for package in container['packages']:
            if (not FRAMEWORK_NAME.match(package['name']) or not FRAMEWORK_VERSION.match(package['version']) or
                    package['architecture'] not in FRAMEWORK_ARCHITECTURES):
                raise ValueError('Malformed framework pin')
            result.append((container, package))
    return result


def uwp_frameworks_extract(container_path, container, package, workdir):
    """The bytes of one pinned package out of its verified container."""
    if container['format'] == 'zip':
        with zipfile.ZipFile(container_path) as archive:
            info = archive.getinfo(package['member'])
            if info.file_size != package['size']:
                raise ValueError('Framework package of unexpected size: ' + package['member'])
            data = archive.read(info)
    else:
        target = workdir / ('cab-' + uuid.uuid4().hex)
        target.mkdir(mode=0o700)
        try:
            subprocess.run(['cabextract', '-q', '-F', package['member'], '-d', str(target), str(container_path)],
                           env={'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8'}, stdin=subprocess.DEVNULL,
                           capture_output=True, timeout=300, check=True)
            produced = [p for p in target.rglob('*')]
            member = target / package['member']
            if produced != [member] or member.is_symlink() or not member.is_file():
                raise ValueError('Unexpected cabinet extraction result: ' + package['member'])
            if member.stat().st_size != package['size']:
                raise ValueError('Framework package of unexpected size: ' + package['member'])
            data = member.read_bytes()
        finally:
            shutil.rmtree(target, ignore_errors=True)
    if len(data) != package['size'] or hashlib.sha256(data).hexdigest() != package['sha256']:
        raise ValueError('Framework package hash mismatch: ' + package['member'])
    return data


def uwp_frameworks_check_identity(data, package, publisher):
    name, version, architecture, declared_publisher, framework = appx_identity(data)
    if (name, version, architecture, declared_publisher) != (package['name'], package['version'],
                                                             package['architecture'], publisher):
        raise ValueError(f'Framework package identity differs from its pin: {name} {version} {architecture}')
    if not framework:
        raise ValueError(f'{name} is not a framework package')
    return appx_full_name(name, version, architecture, publisher)


FRAMEWORK_RECORD_MAX_BYTES = 60000   # the server reads a record of 64 KiB at most
UWP_RECORD_LIMIT = 65536
FRAMEWORK_CLASS_ID = re.compile(r'^[A-Za-z0-9._-]{1,255}$')
FRAMEWORK_SERVER_COMPONENT = re.compile(r'^[A-Za-z0-9_()+,. -]+$')


def appx_manifest_classes(manifest):
    """[(class id, server path)] of the in-process servers of a package manifest (windows.activatableClass.inProcessServer),
    the path relative to the package and backslash separated. A class whose id or path the server would refuse is left
    out."""
    root = xml_fromstring(manifest)
    classes = []
    for server in root.iter():
        if server.tag.rsplit('}', 1)[-1] != 'InProcessServer':
            continue
        path = None
        for child in server:
            if child.tag.rsplit('}', 1)[-1] == 'Path' and child.text:
                path = child.text.strip().replace('/', '\\')
        parts = path.split('\\') if path else []
        if (not parts or len(path) > 512 or any(not FRAMEWORK_SERVER_COMPONENT.match(part) or part in ('.', '..') or
                                                 part.endswith(('.', ' ')) for part in parts)):
            continue
        for child in server:
            if child.tag.rsplit('}', 1)[-1] != 'ActivatableClass':
                continue
            class_id = child.attrib.get('ActivatableClassId', '')
            if FRAMEWORK_CLASS_ID.match(class_id) and (class_id, path) not in classes:
                classes.append((class_id, path))
    return classes


def uwp_frameworks_record(full_name, publisher, manifest=b''):
    """The catalog record (UTF-16LE, no byte order mark), as wexappx writes it, for a framework: the classes of its
    in-process servers are in it, so that the packages that depend on it can activate them."""
    text = (f'WEXAPPX 1\nroot\t{FRAMEWORK_WINDOWS_STORE}\\{full_name}\npublisher\t{publisher}\nframework\t1\n')
    if manifest:
        for class_id, path in appx_manifest_classes(manifest):
            line = f'class\t{class_id}\t{path}\n'
            if (len(text) + len(line)) * 2 > FRAMEWORK_RECORD_MAX_BYTES:
                break
            text += line
    if any(ord(ch) < 0x20 and ch not in '\t\n' or ord(ch) > 0x7e for ch in text):
        raise ValueError('Framework record is not plain ASCII')
    return text.encode('utf-16-le')


def uwp_frameworks_unpack(data, machine, full_name, gid):
    """Unpack one package into the machine's WindowsApps: staged in the root-only area, published by one rename.
    Returns {member: [size, sha256]}."""
    area = machine_stage_area(machine)
    stage = area / ('framework-' + uuid.uuid4().hex)
    stage.mkdir(mode=0o700)
    tree = stage / 'tree'
    tree.mkdir(mode=0o700)
    files, seen = {}, set()
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as package:
            members = package.infolist()
            if len(members) > FRAMEWORK_MAX_MEMBERS or sum(m.file_size for m in members) > FRAMEWORK_MAX_BYTES:
                raise ValueError('Framework package exceeds its member limits')
            for member in members:
                if member.is_dir():
                    continue
                relative = appx_member_path(member.filename)
                if relative is None or (member.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('Unsafe framework package member: ' + member.filename)
                if relative.lower() in seen:
                    raise ValueError('Duplicate framework package member: ' + member.filename)
                seen.add(relative.lower())
                target = tree / relative
                target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                digest, size = hashlib.sha256(), 0
                with package.open(member) as source, open(target, 'xb') as out:
                    while chunk := source.read(1 << 20):
                        size += len(chunk)
                        if size > member.file_size:
                            raise ValueError('Framework package member larger than declared: ' + member.filename)
                        digest.update(chunk)
                        out.write(chunk)
                files[relative] = [size, digest.hexdigest()]
        if 'AppxManifest.xml' not in files:
            raise ValueError('Framework package without a manifest')
        machine_stage_finalize(tree, gid)
        store = machine_open_path(machine, ('drive_c', 'Program Files'), gid, ('WindowsApps',))
        try:
            machine_publish(tree, store, full_name, area)
        finally:
            os.close(store)
        return files
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def uwp_frameworks_catalog_fd(machine, gid, create=True):
    """The descriptor of the machine's catalog of framework records (made when missing); the caller closes it."""
    return machine_open_path(machine, ('drive_c', 'ProgramData') if create else tuple(FRAMEWORK_CATALOG.split('/')), gid,
                             ('WineExplorer', 'appx-catalog') if create else ())


def uwp_frameworks_catalog(machine, gid):
    """The catalog's Path (made when missing)."""
    os.close(uwp_frameworks_catalog_fd(machine, gid))
    return machine / FRAMEWORK_CATALOG


def uwp_frameworks_publish_record(machine, gid, full_name, publisher, manifest):
    catalog = uwp_frameworks_catalog_fd(machine, gid)
    try:
        machine_write_file(catalog, full_name + '.wexappx', uwp_frameworks_record(full_name, publisher, manifest), gid)
    finally:
        os.close(catalog)


def verify_uwp_frameworks(machine, data, full=False):
    """Every package of the receipt: its record as written, its files with their sizes (digests too when full)."""
    packages, publisher = data.get('packages'), data.get('publisher')
    if not isinstance(packages, dict) or not packages or not isinstance(publisher, str):
        raise ValueError('Malformed UWP frameworks receipt')
    for full_name, files in packages.items():
        if not re.match(r'^[A-Za-z0-9.-]+_[0-9.]+_[a-z0-9]+__[0-9a-z]{13}$', full_name) or not isinstance(files, dict):
            raise ValueError('Malformed UWP frameworks receipt')
        try:
            root = machine_open_for_reading(machine, tuple(FRAMEWORK_STORE.split('/')) + (full_name,))
        except ValueError:
            raise ValueError('Framework directory missing: ' + full_name) from None
        try:
            try:
                manifest = machine_read_regular(root, ['AppxManifest.xml'])
            except ValueError:
                raise ValueError('Framework manifest missing: ' + full_name) from None
            try:
                catalog = machine_open_for_reading(machine, tuple(FRAMEWORK_CATALOG.split('/')))
                try:
                    record = machine_read_regular(catalog, [full_name + '.wexappx'], UWP_RECORD_LIMIT)
                finally:
                    os.close(catalog)
            except ValueError:
                record = None
            if record is None or record != uwp_frameworks_record(full_name, publisher, manifest):
                raise ValueError('Framework registration missing or changed: ' + full_name)
            for relative, (size, digest) in files.items():
                try:
                    fd = machine_open_regular(root, relative.split('/'))
                except ValueError:
                    raise ValueError(f'Framework file missing or changed: {full_name}/{relative}') from None
                try:
                    if os.fstat(fd).st_size != size:
                        raise ValueError(f'Framework file missing or changed: {full_name}/{relative}')
                    if (full or relative == 'AppxManifest.xml') and machine_fd_sha256(fd) != digest:
                        raise ValueError(f'Framework file changed: {full_name}/{relative}')
                finally:
                    os.close(fd)
        finally:
            os.close(root)


def refresh_uwp_frameworks_records(machine=None):
    """Update-time repair, as root, under the components lock: an installation from before the class lines (a record
    without them) gets its records rewritten from the installed AppxManifest.xml. Only a package that matches its
    receipt (directory, files with their sizes, the manifest's digest) and whose record is exactly the old format is
    touched; anything else is left alone. Everything is read and written through directory descriptors opened without
    following links, each file read once (the digest and the parse see the same bytes). The receipt holds no digest of a
    record, so it needs no change. Returns (refreshed, skipped): lists of full names, and of (full name, reason)."""
    machine = machine or MACHINE
    refreshed, skipped = [], []
    receipts_dir = machine.parent / 'component-receipts'
    receipt_file = receipts_dir / 'uwp-frameworks.json'
    if not receipt_file.is_file() or receipt_file.is_symlink():
        return refreshed, skipped                    # the component is not installed
    with _components_lock(machine):
        for path in (receipts_dir, receipt_file):
            info = path.lstat()
            if path.is_symlink() or info.st_uid != UWP_XAML_OWNER or info.st_mode & 0o022:
                raise ValueError('Untrusted UWP frameworks receipt')
        if receipt_file.stat().st_size > 1024 * 1024:
            raise ValueError('Untrusted UWP frameworks receipt')
        data = json.loads(receipt_file.read_text(encoding='utf-8'))
        packages, publisher = data.get('packages'), data.get('publisher')
        if (data.get('verb') != 'uwp-frameworks' or data.get('execution_policy') != UWP_FRAMEWORKS_POLICY or
                not isinstance(packages, dict) or not packages or not isinstance(publisher, str)):
            raise ValueError('Malformed UWP frameworks receipt')
        gid = machine_admin_gid(machine)
        try:
            store = machine_open_path(machine, tuple(FRAMEWORK_STORE.split('/')), gid)
            catalog = uwp_frameworks_catalog_fd(machine, gid, create=False)
        except (OSError, ValueError) as error:
            return refreshed, [(name, f'the framework store or its catalog cannot be opened: {error}') for name in packages]
        try:
            for full_name, files in packages.items():
                if not re.match(r'^[A-Za-z0-9.-]+_[0-9.]+_[a-z0-9]+__[0-9a-z]{13}$', full_name) or not isinstance(files, dict):
                    skipped.append((full_name, 'malformed receipt entry'))
                    continue
                try:
                    root = machine_open_dir(store, full_name, gid)
                    try:
                        for relative, (size, digest) in files.items():
                            if relative == 'AppxManifest.xml':
                                continue
                            fd = machine_open_regular(root, relative.split('/'))
                            try:
                                if os.fstat(fd).st_size != size:
                                    raise ValueError('a package file changed: ' + relative)
                            finally:
                                os.close(fd)
                        manifest = machine_read_regular(root, ['AppxManifest.xml'])
                    finally:
                        os.close(root)
                    if 'AppxManifest.xml' not in files or hashlib.sha256(manifest).hexdigest() != files['AppxManifest.xml'][1] \
                            or len(manifest) != files['AppxManifest.xml'][0]:
                        raise ValueError('the manifest changed')
                    current = machine_read_regular(catalog, [full_name + '.wexappx'], UWP_RECORD_LIMIT)
                    if current == uwp_frameworks_record(full_name, publisher, manifest):
                        continue                         # up to date
                    if current != uwp_frameworks_record(full_name, publisher):
                        raise ValueError('the record is not in the old format')
                    machine_write_file(catalog, full_name + '.wexappx', uwp_frameworks_record(full_name, publisher, manifest), gid)
                    refreshed.append(full_name)
                except (OSError, ValueError, ET.ParseError) as error:
                    skipped.append((full_name, str(error)))
        finally:
            os.close(store)
            os.close(catalog)
    return refreshed, skipped


def uwp_frameworks_remove(machine, full_names):
    """Records first (the packages stop being visible), then their directories; missing ones are fine. The machine root
    is worked on by directory descriptors, each removal is one rename into the root-only staging area."""
    gid = machine_admin_gid(machine)
    area = machine_stage_area(machine)
    try:
        catalog = uwp_frameworks_catalog_fd(machine, gid, create=False)
    except FileNotFoundError:
        catalog = None
    if catalog is not None:
        try:
            for full_name in full_names:
                try:
                    os.unlink(full_name + '.wexappx', dir_fd=catalog)
                except FileNotFoundError:
                    pass
        finally:
            os.close(catalog)
    try:
        store = machine_open_path(machine, tuple(FRAMEWORK_STORE.split('/')), gid)
    except FileNotFoundError:
        return
    try:
        for full_name in full_names:
            machine_remove(store, full_name, area)
    finally:
        os.close(store)


def uwp_frameworks_previous(receipt_dir, verb):
    """The packages an earlier installation's receipt lists."""
    receipt = receipt_dir / (verb + '.json')
    if receipt.is_symlink() or not receipt.is_file() or receipt.stat().st_uid != UWP_XAML_OWNER:
        return []
    try:
        packages = json.loads(receipt.read_text(encoding='utf-8')).get('packages')
    except (OSError, ValueError, AttributeError):
        return []
    if not isinstance(packages, dict):
        return []
    return [name for name in packages if isinstance(name, str) and re.match(r'^[A-Za-z0-9._-]+$', name)]


def install_uwp_frameworks(verb, accepted_digest):
    _, current = license_text(verb)
    if current != accepted_digest:
        raise ValueError('Terms changed; review and accept the current text')
    with _machine_maintenance() as wine:
        if is_installed(verb, accepted_digest, machine=MACHINE):
            return
        wine.require_idle()
        if CACHE.is_symlink():
            raise ValueError('Refusing symlinked component cache')
        CACHE.mkdir(mode=0o700, exist_ok=True)
        if CACHE.stat().st_uid != 0 or CACHE.stat().st_mode & 0o022:
            raise ValueError('Component cache must be root-owned and not writable by others')
        install_uwp_frameworks_into(verb, MACHINE, CACHE, current)


def install_uwp_frameworks_into(verb, machine, cache, terms_digest):
    """Download (or reuse), check and publish every package, then the receipt; on failure nothing of this attempt
    stays and no receipt claims an earlier one."""
    meta = COMPONENTS_METADATA[verb]
    publisher = meta['publisher_name']
    gid = machine_admin_gid(machine)
    receipt_dir = machine.parent / 'component-receipts'
    previous = uwp_frameworks_previous(receipt_dir, verb)
    pins = uwp_frameworks_packages(verb)
    # everything is downloaded and checked before anything is published
    containers, contents = {}, []
    work = cache / verb / ('.work-' + uuid.uuid4().hex)
    try:
        for container, package in pins:
            if container['name'] not in containers:
                containers[container['name']] = cache_pinned_download(verb, container, cache, FRAMEWORK_REDIRECTS,
                                                                      'UWP frameworks')
            if not work.exists():
                work.mkdir(mode=0o700)
            data = uwp_frameworks_extract(containers[container['name']], container, package, work)
            contents.append((uwp_frameworks_check_identity(data, package, publisher), data))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    published = []
    data_receipt = {
        'verb': verb,
        'title': meta.get('title', verb),
        'sha256': terms_digest,
        'terms_sha256': terms_digest,
        'execution_policy': UWP_FRAMEWORKS_POLICY,
        'component_identity': component_identity(verb),
        'publisher': publisher,
        'pins': [package['sha256'] for _, package in pins],
        'packages': {},
        'installed_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
    }
    try:
        for full_name, data in contents:
            published.append(full_name)
            data_receipt['packages'][full_name] = uwp_frameworks_unpack(data, machine, full_name, gid)
            with zipfile.ZipFile(io.BytesIO(data)) as package:
                manifest = package.read('AppxManifest.xml')
            uwp_frameworks_publish_record(machine, gid, full_name, publisher, manifest)
        verify_uwp_frameworks(machine, data_receipt, full=True)
    except BaseException:
        try:
            uwp_frameworks_remove(machine, sorted(set(published) | set(previous)))
        except Exception as error:
            print(f'UWP frameworks rollback: removal failed: {error}', file=sys.stderr)
        try:
            uwp_xaml_drop_receipt(receipt_dir, verb)
        except OSError as error:
            print(f'UWP frameworks rollback: receipt removal failed: {error}', file=sys.stderr)
        raise
    # versions an earlier installation published and this one does not
    stale = [name for name in previous if name not in data_receipt['packages']]
    if stale:
        try:
            uwp_frameworks_remove(machine, stale)
        except Exception as error:
            print(f'UWP frameworks upgrade: removing {len(stale)} packages failed: {error}', file=sys.stderr)
    receipt_dir.mkdir(mode=0o755, exist_ok=True)
    if receipt_dir.is_symlink() or receipt_dir.stat().st_uid != UWP_XAML_OWNER or receipt_dir.stat().st_mode & 0o022:
        raise ValueError('Untrusted component receipt directory')
    atomic_json(receipt_dir / (verb + '.json'), data_receipt, mode=0o644)
    (receipt_dir / (verb + '.sha256')).write_text(terms_digest + '\n', encoding='ascii')


def uninstall_uwp_frameworks(verb, machine):
    receipt = machine.parent / 'component-receipts' / (verb + '.json')
    if receipt.is_symlink() or not receipt.is_file() or receipt.stat().st_uid != UWP_XAML_OWNER:
        raise ValueError(f'{verb} is not installed')
    uwp_frameworks_remove(machine, uwp_frameworks_previous(receipt.parent, verb))
    receipt.unlink()
    (receipt.parent / (verb + '.sha256')).unlink(missing_ok=True)


def uninstall(verb):
    """Remove exactly what the receipt lists: the registered classes, ComponentPath, the directory, the receipt."""
    validate_verb(verb)
    policy = COMPONENTS_METADATA[verb].get('execution_policy')
    if policy not in (UWP_XAML_POLICY, UWP_FRAMEWORKS_POLICY, POWERSHELL_POLICY):
        raise ValueError(f'{verb} cannot be uninstalled by this tool')
    if os.getuid() != 0:
        raise ValueError('The installer backend requires authenticated elevation')
    if policy == POWERSHELL_POLICY:
        with _machine_maintenance() as wine:
            wine.require_idle()
            uninstall_powershell(verb)
        return
    if policy == UWP_FRAMEWORKS_POLICY:
        with _machine_maintenance() as wine:
            wine.require_idle()
            uninstall_uwp_frameworks(verb, MACHINE)
        return
    with _machine_maintenance() as wine:
        wine.require_idle()
        receipt = MACHINE.parent / 'component-receipts' / (verb + '.json')
        if receipt.is_symlink() or not receipt.is_file() or receipt.stat().st_uid != 0:
            raise ValueError(f'{verb} is not installed')
        data = json.loads(receipt.read_text(encoding='utf-8'))
        arch, classes = data.get('payload_architecture'), data.get('classes')
        if arch != 'x64' or not isinstance(classes, list) or any(not UWP_XAML_CLASS.match(c) for c in classes):
            raise ValueError('Malformed UWP XAML receipt')
        uwp_xaml_unregister(wine, classes)
        uwp_xaml_remove_directory(MACHINE, arch)
        receipt.unlink()
        (receipt.parent / (verb + '.sha256')).unlink(missing_ok=True)


def install(verb, accepted_digest):
    validate_verb(verb)
    if COMPONENTS_METADATA.get(verb, {}).get('execution_policy') in (UWP_XAML_POLICY, UWP_FRAMEWORKS_POLICY,
                                                                      POWERSHELL_POLICY):
        if COMPONENTS_METADATA[verb].get('disabled'):
            raise ValueError(f'{verb} is disabled: {COMPONENTS_METADATA[verb].get("disabled_reason")}')
        if os.getuid() != 0:
            raise ValueError('The installer backend requires authenticated elevation')
        if COMPONENTS_METADATA[verb]['execution_policy'] == POWERSHELL_POLICY:
            return install_powershell(verb, accepted_digest)
        if COMPONENTS_METADATA[verb]['execution_policy'] == UWP_FRAMEWORKS_POLICY:
            return install_uwp_frameworks(verb, accepted_digest)
        return install_uwp_xaml(verb, accepted_digest)
    meta = COMPONENTS_METADATA.get(verb, {})
    if meta.get('disabled'):
        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
    if os.getuid() != 0:
        raise ValueError('The installer backend requires authenticated elevation')
    validate_winetricks_integrity()
    validate_recipe_files(verb)
    _, current = license_text(verb)
    if current != accepted_digest:
        raise ValueError('Vendor terms changed; review and accept the current text')
    if MACHINE.is_symlink() or MACHINE.stat().st_uid != 0:
        raise ValueError('Machine prefix must be a root-owned real directory')
    # Neither arbitrary prefixes, DLL overrides, executables, display sockets nor
    # user-controlled caches are inherited across the privilege boundary (MachineWine.environment).
    with _machine_maintenance() as wine:
        if is_installed(verb, accepted_digest, machine=MACHINE):
            return
        for need in meta.get('requires', ()):
            if not is_installed(need, machine=MACHINE):
                raise ValueError(f'{meta["title"]} needs {COMPONENTS_METADATA[need]["title"]}: install it first')
        # Do not kill somebody else's maintenance client or wait for a desktop.
        wine.require_idle()
        cache = CACHE
        if cache.is_symlink():
            raise ValueError('Refusing symlinked component cache')
        cache.mkdir(mode=0o700, exist_ok=True)
        if cache.stat().st_uid != 0 or cache.stat().st_mode & 0o022:
            raise ValueError('Component cache must be root-owned and not writable by others')
        program = '/usr/bin/winetricks'
        execution_digest = None
        isolate = True      # every pinned recipe runs without a network
        if verb in ('vcrun2022', 'd3dcompiler_47'):
            if verb == 'vcrun2022':
                recipe = approved_runtime_recipe()
                cache_runtime_payloads(cache)
            else:
                recipe = approved_d3dcompiler_recipe()
                cache_d3dcompiler_payloads(cache)
            execution_digest = hashlib.sha256(recipe).hexdigest()
            with tempfile.NamedTemporaryFile(dir=cache, prefix='.approved-' + verb + '-', delete=False) as out:
                script = Path(out.name)
                os.fchmod(out.fileno(), 0o700)
                out.write(recipe)
                out.flush()
                os.fsync(out.fileno())
            approved_script = cache / ('approved-' + verb + '.sh')
            os.replace(script, approved_script)
            program = str(wine.stage(cache / verb, approved_script) / approved_script.name)
            argv = ['/usr/bin/xvfb-run', '-a', program, '-q', verb]
        elif verb == 'corefonts':
            cache_corefonts_payloads(cache)
            wine.stage(cache / verb)
            corefonts_drop_links(MACHINE)
            argv = ['/usr/bin/xvfb-run', '-a', program, '-q', verb]
        elif verb == 'webview2':
            installer = cache_webview2_payload(cache)
            execution_digest = hashlib.sha256(
                (COMPONENTS_METADATA['webview2']['execution_policy'] + ' ' +
                 COMPONENTS_METADATA['webview2']['payloads'][0]['sha256']).encode()).hexdigest()
            installer = wine.stage(cache / verb) / installer.relative_to(cache)
            argv = ['/usr/bin/xvfb-run', '-a', wine.WINE, str(installer), '/silent', '/install']
        elif verb in EDGEUPDATE_APPS:
            payload = meta['payloads'][0]
            installer = cache_pinned_download(verb, payload, cache, ('https://' + payload['host'] + '/',),
                                              meta['title'], deadline=1800)
            execution_digest = hashlib.sha256((meta['execution_policy'] + ' ' + payload['sha256']).encode()).hexdigest()
            installer = wine.stage(cache / verb) / installer.relative_to(cache)
            argv = ['/usr/bin/xvfb-run', '-a', wine.WINE, 'msiexec', '/i', msi_dos_path(MACHINE, installer),
                    '/qn', '/norestart']
        else:
            # winetricks' own recipe, which downloads (only for components enabled without a pinned one)
            isolate = False
            argv = ['/usr/bin/xvfb-run', '-a', program, '-q', verb]
        regedit_src = MACHINE / 'drive_c/windows/regedit.exe'
        if regedit_src.is_file():
            ensure_installer_regedit(MACHINE)
        # Prefix ownership and default ACLs stay intact: do not recursively
        # chown live user prefixes. Record the real verb result, never success
        # from a progress-dialog pipeline. All package terms were accepted above.
        with protect_builtins(MACHINE, wine) as recovery:
            if recovery is not None:
                recovery['safe_to_restore'] = False
            # Without a network; the lock and builtin snapshots stay until Wine has also stopped.
            result = wine.run(argv, timeout=1800, isolate=isolate)
            try:
                if verb in EDGEUPDATE_APPS and result.returncode == 0:
                    # Keep the runtime on the version we verified: no machine-wide
                    # self-update from a system-level updater (see WEBVIEW2_POLICY).
                    try:
                        for name, data in edgeupdate_policy(verb):
                            wine.run(['/usr/bin/xvfb-run', '-a', wine.WINE, 'reg', 'add', WEBVIEW2_POLICY_KEY,
                                      '/v', name, '/t', 'REG_DWORD', '/d', str(data), '/f'],
                                     timeout=120, check=True, stdout=subprocess.DEVNULL)
                    except (subprocess.SubprocessError, OSError):
                        # The installer's updater keeps the server busy: stop what is left
                        # before reporting, or the lock-holding prefix never idles.
                        wine.stop(check=False)
                        if recovery is not None:
                            recovery['safe_to_restore'] = True
                        raise ValueError(f'{meta["title"]} installed but its update policy could not be set; '
                                         'machine-prefix Wine stopped')
                # Microsoft's installer leaves its update service running: stop what is
                # left once the installer succeeded, and after a deadline.
                if result.returncode in (124, 137) or (verb in EDGEUPDATE_APPS and result.returncode == 0):
                    wine.stop()
                wine.wait_idle()
            except subprocess.TimeoutExpired:
                wine.stop()
                if recovery is not None:
                    recovery['safe_to_restore'] = True
                raise ValueError('Installer did not finish; machine-prefix Wine stopped; installation may be partial')
            if recovery is not None:
                recovery['safe_to_restore'] = True
            if result.returncode == 0:
                if verb == 'vcrun2022':
                    repair_runtime_artifacts(MACHINE, cache)
                elif verb == 'corefonts':
                    corefonts_publish_modes(MACHINE)
                elif verb == 'd3dcompiler_47':
                    repair_d3dcompiler_artifacts(MACHINE, cache)
        if result.returncode:
            raise ValueError(f'{verb} failed (exit {result.returncode}); installation may be partial; see console output')
        if verb == 'vcrun2022':
            verify_runtime_artifacts(MACHINE)
        elif verb == 'corefonts':
            verify_corefonts_artifacts(MACHINE)
        elif verb == 'd3dcompiler_47':
            verify_d3dcompiler_artifacts(MACHINE)
        elif verb in EDGEUPDATE_APPS:
            verify_webview2_artifacts(MACHINE, verb)
        receipt = MACHINE.parent / 'component-receipts'
        receipt.mkdir(mode=0o755, exist_ok=True)
        if receipt.is_symlink() or receipt.stat().st_uid != 0 or receipt.stat().st_mode & 0o022:
            raise ValueError('Untrusted component receipt directory')
        # Receipts contain package identities, not license text, credentials or
        # private operation details. Users need them to validate shared defaults.
        receipt.chmod(0o755)
        receipt_data = {
            'verb': verb,
            'title': meta.get('title', verb),
            'sha256': current,
            'terms_sha256': current,
            'winetricks_version': APPROVED_WINETRICKS_VERSION,
            'winetricks_sha256': APPROVED_WINETRICKS_SHA256,
            'architecture': 'wow64',
            'installed_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
            'recipe_files': meta.get('recipe_files', []),
            'component_identity': component_identity(verb),
            'execution_recipe_sha256': execution_digest,
        }
        # Publish complete records atomically; a truncated JSON is not success.
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=receipt,
                                         prefix='.' + verb, delete=False) as out:
            temporary_receipt = Path(out.name)
            os.fchmod(out.fileno(), 0o644)
            json.dump(receipt_data, out, indent=2)
            out.write('\n')
            out.flush()
            os.fsync(out.fileno())
        try:
            os.replace(temporary_receipt, receipt / (verb + '.json'))
        finally:
            temporary_receipt.unlink(missing_ok=True)
        # The digest file too (a plain write_text was left empty by a power-off right after an install), and then
        # the directory, so that both renames survive one.
        with tempfile.NamedTemporaryFile(mode='w', encoding='ascii', dir=receipt,
                                         prefix='.' + verb, delete=False) as out:
            temporary_receipt = Path(out.name)
            os.fchmod(out.fileno(), 0o644)
            out.write(current + '\n')
            out.flush()
            os.fsync(out.fileno())
        try:
            os.replace(temporary_receipt, receipt / (verb + '.sha256'))
        finally:
            temporary_receipt.unlink(missing_ok=True)
        directory = os.open(receipt, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)


def format_size(total):
    return f'{total / (1 << 20):.1f} MiB'


def acquire_review_terms(verb, gui):
    """Later-launch review: explicit permission, privileged download, cached terms.

    Returns (text, fingerprint), or None when the administrator declines the
    download. Reading cached terms never needs the network or elevation.
    """
    try:
        return license_text(verb)
    except (ReviewRequired, StaleTermsError):
        pass
    if not is_admin():
        raise ValueError('Administrator elevation required to download packages for review')
    review = review_meta(verb)
    meta = COMPONENTS_METADATA[verb]
    host = urllib.parse.urlsplit(review['url_prefix']).hostname
    message = (f'Download {format_size(review_total_bytes(verb))} ({len(review_packages(verb))} packages '
               f'from {host}, published by {meta["publisher"]}) so the license terms of '
               f'{COMPONENTS[verb]} can be reviewed?\n\nNo installer or package code will run and '
               'nothing will be installed. Installation requires a separate acceptance after you '
               'have read the terms.')
    if gui:
        permitted = subprocess.run(['zenity', '--question', '--title=Download for review',
                                    '--width=520', '--text=' + message]).returncode == 0
    else:
        print(message)
        permitted = input('Download for review? Type yes: ').strip() == 'yes'
    if not permitted:
        return None
    command = [BACKEND, '--review-download', verb]
    if os.getuid() != 0:
        command.insert(0, '/usr/bin/pkexec')
    # The worker enforces REVIEW_DEADLINE_SECONDS itself; the margin keeps a wedged
    # subprocess from blocking this UI thread indefinitely.
    try:
        result = subprocess.run(command, timeout=REVIEW_DEADLINE_SECONDS + 60)
    except subprocess.TimeoutExpired as error:
        raise ValueError(f'The review download for {verb} exceeded its time limit') from error
    if result.returncode != 0:
        raise ValueError(f'Failed to download {verb} for review')
    try:
        return license_text(verb)
    except (ReviewRequired, StaleTermsError) as error:
        raise ValueError(f'The review download for {verb} completed without valid terms') from error


def review_only(verb, gui):
    """Show embedded terms for a component whose installation is not yet enabled."""
    acquired = acquire_review_terms(verb, gui)
    if acquired is None:
        return
    text, _ = acquired
    note = f'Installation of {COMPONENTS[verb]} is not available yet. Reviewing these terms installed nothing.'
    if gui:
        subprocess.run(['zenity', '--text-info', '--title=' + COMPONENTS[verb] + ' - terms for review',
                        '--width=760', '--height=560'], input=text, text=True)
        subprocess.run(['zenity', '--info', '--text=' + note])
    else:
        print(text)
        print(note)


def consent(verb, gui):
    validate_verb(verb)
    meta = COMPONENTS_METADATA.get(verb, {})
    if meta.get('disabled'):
        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
    if review_available(verb):
        acquired = acquire_review_terms(verb, gui)
        if acquired is None:
            return None
        text, sha = acquired
    else:
        try:
            text, sha = license_text(verb)
        except ValueError:
            if not is_admin():
                raise ValueError('Administrator elevation required to prepare vendor terms')
            command = [BACKEND, '--prepare-terms', verb]
            if os.getuid() != 0:
                command.insert(0, '/usr/bin/pkexec')
            res = subprocess.run(command)
            if res.returncode != 0:
                raise ValueError(f'Failed to prepare authentic vendor terms for {verb}')
            text, sha = license_text(verb)
    if gui:
        result = subprocess.run(['zenity', '--text-info', '--title=' + COMPONENTS[verb],
                                 '--width=760', '--height=560',
                                 '--checkbox=I accept these vendor license terms'],
                                input=text, text=True)
        return sha if result.returncode == 0 else None
    print(text)
    return sha if input(f'Accept these terms for {verb}? Type yes: ').strip() == 'yes' else None


def with_prerequisites(verbs, installed=None):
    """VERBS with the components they need that are not installed yet, each after what it needs (COMPONENTS order,
    which lists a prerequisite first). Every added one still asks for its own acceptance of terms."""
    installed = get_installed_receipts() if installed is None else installed
    wanted = set(verbs)
    for verb in verbs:
        wanted.update(need for need in COMPONENTS_METADATA[validate_verb(verb)].get('requires', ())
                      if need not in installed)
    return [verb for verb in COMPONENTS if verb in wanted]


def frontend(verbs, gui):
    if not is_admin():
        raise ValueError('Windows Components is available to administrators only')

    for verb in verbs:
        validate_verb(verb)
        meta = COMPONENTS_METADATA.get(verb, {})
        if meta.get('disabled'):
            raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
    review_verbs = []
    if gui:
        command = ['zenity', '--list', '--checklist', '--title=Windows Components',
                   '--column=Install', '--column=Component', '--column=Description',
                   '--print-column=2', '--separator=|', '--width=720', '--height=360']
        installed = get_installed_receipts()
        for verb, name in COMPONENTS.items():
            checked = 'TRUE' if verb in installed else 'FALSE'
            meta = COMPONENTS_METADATA[verb]
            description = name
            if meta.get('disabled'):
                if review_available(verb):
                    # A review-only component gets no Install checkbox: checking it could
                    # be mistaken for an installation acceptance. It is offered by the
                    # separate review dialog below instead.
                    continue
                description += ' [Unavailable: ' + meta.get('disabled_reason', 'verification pending') + ']'
                checked = 'FALSE'
            command.extend([checked, verb, description])
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            return
        verbs = [verb for verb in result.stdout.strip().split('|') if verb]
        # Review selection is separate from the install checklist: a plain list with
        # no checkboxes, so selecting terms can never accept an installation.
        review_rows = []
        for verb, name in COMPONENTS.items():
            if review_available(verb):
                note = ' - installation unavailable' if COMPONENTS_METADATA[verb].get('disabled') else ''
                review_rows.append([verb, f'{name}{note}'])
        if review_rows:
            review_command = ['zenity', '--list', '--title=Review license terms (nothing installs)',
                              '--column=Component', '--column=Description',
                              '--print-column=1', '--separator=|', '--width=720', '--height=360']
            for row in review_rows:
                review_command.extend(row)
            review_result = subprocess.run(review_command, capture_output=True, text=True)
            if not review_result.returncode and review_result.stdout.strip():
                review_verbs = [verb for verb in review_result.stdout.strip().split('|') if verb]
    verbs = with_prerequisites(verbs)
    for verb in verbs:
        validate_verb(verb)
        meta = COMPONENTS_METADATA.get(verb, {})
        if meta.get('disabled'):
            raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
        sha = consent(verb, gui)
        if sha is None:
            return
        # pkexec authenticates an administrator and sanitizes the environment.
        # No broad passwordless policy is installed.
        command = [BACKEND, '--install', verb, sha]
        if os.getuid() != 0:
            command.insert(0, '/usr/bin/pkexec')
        result = subprocess.run(command)
        if result.returncode:
            raise ValueError(f'{verb} was not installed successfully (exit {result.returncode})')
    for verb in review_verbs:
        validate_verb(verb)
        if not review_available(verb):
            raise ValueError(f'{verb} has no license terms to review')
        review_only(verb, gui=gui)
    if gui and verbs:
        subprocess.run(['zenity', '--info', '--text=Selected components installed successfully.'])


def checked_operation_directory():
    OPERATION_STATE.mkdir(mode=0o700, parents=True, exist_ok=True)
    info = OPERATION_STATE.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o077:
        raise PermissionError('OOBE operation directory must be private and root-owned')
    return OPERATION_STATE


def read_operation_journal():
    journal = OPERATION_STATE / 'operations.json'
    if not journal.exists() and not journal.is_symlink():
        return {}
    info = journal.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o077 or info.st_size > 1024 * 1024:
        raise PermissionError('Untrusted OOBE operation journal')
    records = json.loads(journal.read_text(encoding='utf-8'))
    if not isinstance(records, dict):
        raise ValueError('Invalid operation journal')
    for key, record in records.items():
        if (not isinstance(record, dict) or not re.fullmatch(r'op_[a-z0-9_]{1,120}', key)
                or record.get('verb') not in COMPONENTS
                or record.get('status') not in ('RUNNING', 'UNKNOWN', 'SUCCESS', 'FAILED')):
            raise ValueError('Invalid operation journal record')
    return records


@contextmanager
def coordinator_lease():
    directory = checked_operation_directory()
    descriptor = os.open(directory / '.coordinator.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'a') as lease:
        try:
            fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('Stop the OOBE coordinator before administrative reconciliation or restart')
        yield


def reconcile_operation(operation, note):
    """Acknowledge an inspected interrupted install, never infer success/rollback."""
    if os.getuid() != 0:
        raise PermissionError('Operation reconciliation requires authenticated root administration')
    if MACHINE.is_symlink() or not MACHINE.is_dir() or MACHINE.stat().st_uid != 0:
        raise ValueError('Machine prefix must be a root-owned real directory')
    if not re.fullmatch(r'op_[a-z0-9_]{1,120}', operation):
        raise ValueError('Invalid operation ID')
    if not note or not 10 <= len(note.strip()) <= 512 or any(ord(c) < 32 for c in note):
        raise ValueError('Supply an inspection note of 10-512 printable characters (no secrets)')
    with coordinator_lease():
        descriptor = os.open(MACHINE.parent / '.components.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        with os.fdopen(descriptor, 'a') as maintenance:
            fcntl.flock(maintenance, fcntl.LOCK_EX | fcntl.LOCK_NB)
            marker = MACHINE.parent / 'component-recovery-required'
            if marker.exists() or marker.is_symlink():
                raise ValueError('Protected-state recovery is required; retain and inspect marker/snapshots before reconciliation')
            MachineWine(MACHINE).require_idle()
            records = read_operation_journal()
            record = records.get(operation)
            if not record or record['status'] not in ('UNKNOWN', 'RUNNING'):
                raise ValueError('Only an interrupted UNKNOWN/RUNNING operation can be reconciled')
            record.update(status='FAILED', error='Administrator inspected interrupted install; retry permitted, not rolled back',
                          reconciliation={'note': note.strip(), 'uid': os.getuid(), 'time': int(time.time())})
            atomic_json(OPERATION_STATE / 'operations.json', records, 0o600)
    return record


def oobe_coordinator():
    if os.getuid() != 0:
        raise PermissionError('OOBE coordinator requires root execution')
    with coordinator_lease():
        return run_oobe_coordinator()


def run_oobe_coordinator():
    """Privileged coordinator daemon during Setup Out-of-Box Experience."""
    if os.getuid() != 0:
        raise PermissionError('OOBE coordinator requires root execution')
    stage_file_str = os.environ.get('WEX_OOBE_STAGE_FILE', '/run/wex-oobe/stage')
    stage_file = Path(stage_file_str)
    if not active_oobe_stage(stage_file):
        raise PermissionError('OOBE coordinator only permitted during active OOBE stage')

    sock_path_str = os.environ.get('WEX_OOBE_SOCKET', '/run/wex-oobe/coordinator.sock')
    sock_path = Path(sock_path_str)
    sock_path.parent.mkdir(parents=True, exist_ok=True)
    parent_info = sock_path.parent.lstat()
    if not stat.S_ISDIR(parent_info.st_mode) or parent_info.st_uid != 0 or parent_info.st_mode & 0o022:
        raise PermissionError('OOBE endpoint directory must be root-owned and not writable by others')
    os.chmod(sock_path.parent, 0o755)
    if sock_path.exists():
        sock_path.unlink()

    try:
        wexsetup_uid = pwd.getpwnam('wexsetup').pw_uid
    except KeyError:
        raise RuntimeError('Required setup account "wexsetup" does not exist')

    running = True
    active_operations = {}
    # Review downloads never touch the install journal: they change no machine
    # state, create no receipt and are not an installation authorization.
    review_operations = {}
    operations_lock = threading.Lock()
    OPERATION_STATE.mkdir(mode=0o700, parents=True, exist_ok=True)
    state_info = OPERATION_STATE.lstat()
    if not stat.S_ISDIR(state_info.st_mode) or state_info.st_uid != 0 or state_info.st_mode & 0o077:
        raise PermissionError('OOBE operation directory must be private and root-owned')
    journal = OPERATION_STATE / 'operations.json'

    def save_operations():
        records = {key: {k: v for k, v in value.items() if k != 'thread'}
                   for key, value in active_operations.items()}
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8',
                                         dir=OPERATION_STATE, delete=False) as out:
            temporary = Path(out.name)
            json.dump(records, out)
            out.flush()
            os.fsync(out.fileno())
        try:
            os.replace(temporary, journal)
        finally:
            temporary.unlink(missing_ok=True)

    if journal.exists() or journal.is_symlink():
        active_operations = read_operation_journal()
        for info in active_operations.values():
            info['thread'] = None
            if info['status'] == 'RUNNING':
                info['status'] = 'UNKNOWN'
                info['error'] = 'Coordinator restarted during installation; administrator reconciliation required'
        save_operations()

    # Validate/recover persistent state before publishing a live endpoint.
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(sock_path))
    os.chmod(str(sock_path), 0o666)
    server.listen(5)
    server.settimeout(2.0)

    def stop_handler(signum, frame):
        nonlocal running
        running = False
    try:
        signal.signal(signal.SIGTERM, stop_handler)
        signal.signal(signal.SIGINT, stop_handler)
    except ValueError:
        pass

    print('OOBE coordinator listening on', sock_path, file=sys.stderr)

    while running:
        if not active_oobe_stage(stage_file):
            print('OOBE stage expired; coordinator shutting down.', file=sys.stderr)
            break
        try:
            conn, _ = server.accept()
        except socket.timeout:
            continue
        except OSError:
            break

        conn.settimeout(15.0)
        try:
            # Enforce peer credentials: only wexsetup (or root in tests/CLI)
            creds = conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize('3i'))
            pid, uid, gid = struct.unpack('3i', creds)
            if uid != wexsetup_uid and uid != 0:
                print(f'Rejecting unauthorized client UID {uid}', file=sys.stderr)
                conn.close()
                continue

            # Read bounded request line (max 1024 bytes)
            raw_req = b''
            deadline = time.monotonic() + 15.0
            while len(raw_req) < 1024:
                conn.settimeout(max(0.001, deadline - time.monotonic()))
                b = conn.recv(1)
                if b == b'\n':
                    break
                if not b or time.monotonic() >= deadline:
                    raise ValueError('Incomplete coordinator request')
                raw_req += b
            else:
                raise ValueError('Coordinator request exceeds limit')

            line = raw_req.decode('utf-8', errors='replace').strip()
            if not line:
                conn.close()
                continue

            parts = line.split()
            cmd = parts[0] if parts else ''

            if cmd == 'GET_COMPONENTS' and len(parts) == 1:
                installed = get_installed_receipts()
                lines = ['OK']
                for v, meta in COMPONENTS_METADATA.items():
                    inst = '1' if v in installed else '0'
                    reb = '1' if meta['reboot'] else '0'
                    dis = '1' if meta.get('disabled') else '0'
                    dis_reason = meta.get('disabled_reason', '')
                    # Format: verb \t title \t publisher \t description \t size \t reboot \t installed \t disabled \t disabled_reason
                    # Review availability is separate from install eligibility:
                    # 'R:<total bytes>:<cached 0|1>' offers Download-for-review only.
                    rev = '0'
                    if review_available(v):
                        state, total = review_state(v)
                        rev = f"R:{total}:{1 if state == 'cached' else 0}"
                    row = f"{v}\t{meta['title']}\t{meta['publisher']}\t{meta['description']}\t{meta['size']}\t{reb}\t{inst}\t{dis}\t{dis_reason}\t{rev}"
                    lines.append(row)
                conn.sendall(('\n'.join(lines) + '\n').encode('utf-8'))

            elif cmd == 'GET_TERMS' and len(parts) == 2:
                verb = parts[1]
                try:
                    validate_verb(verb)
                    meta = COMPONENTS_METADATA.get(verb, {})
                    if meta.get('disabled') and not review_available(verb):
                        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
                    if review_available(verb) and not meta.get('terms_url'):
                        # Cached, verified terms only. Never an automatic download and
                        # never a silent refresh: stale or tampered terms must surface
                        # as a distinct error the client can act on via START_REVIEW.
                        try:
                            text, sha = license_text(verb)
                        except ReviewRequired as err:
                            conn.sendall(f"ERR NOT_DOWNLOADED {err}\n".encode('utf-8'))
                            continue
                        except StaleTermsError as err:
                            conn.sendall(f"ERR STALE_TERMS {err}\n".encode('utf-8'))
                            continue
                    else:
                        try:
                            text, sha = license_text(verb)
                        except ValueError:
                            text, sha = fetch_vendor_terms(verb)
                    response = f"OK {sha} {len(text.encode('utf-8'))}\n{text}"
                    conn.sendall(response.encode('utf-8'))
                except Exception as err:
                    err_msg = f"ERR {err}\n"
                    conn.sendall(err_msg.encode('utf-8'))

            elif (cmd == 'START_INSTALL' or cmd == 'INSTALL') and len(parts) == 3:
                # Revalidate stage immediately before dispatching privileged install
                if not active_oobe_stage(stage_file):
                    conn.sendall(b"ERR Setup OOBE stage expired; installation rejected\n")
                    try:
                        conn.shutdown(socket.SHUT_WR)
                    except OSError:
                        pass
                    continue
                verb, accepted_sha = parts[1], parts[2]
                try:
                    validate_verb(verb)
                    meta = COMPONENTS_METADATA.get(verb, {})
                    if meta.get('disabled'):
                        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
                    _, current_sha = license_text(verb)
                    if current_sha != accepted_sha:
                        raise ValueError('Vendor terms changed; review and accept the current text')
                except Exception as err:
                    conn.sendall(f"ERR {err}\n".encode('utf-8'))
                    continue

                if is_installed(verb, accepted_sha):
                    conn.sendall(b"OK ALREADY_INSTALLED\n")
                    continue

                with operations_lock:
                    existing_op = None
                    other_running = False
                    for op_id, op_info in active_operations.items():
                        if op_info['status'] == 'UNKNOWN':
                            other_running = True
                        if op_info['status'] == 'RUNNING':
                            if op_info['verb'] == verb:
                                existing_op = (op_id, op_info)
                            else:
                                other_running = True
                    if existing_op:
                        op_id, op_info = existing_op
                        if op_info['sha'] == accepted_sha:
                            conn.sendall(f"OK {op_id} STARTED\n".encode('utf-8'))
                            continue
                        else:
                            conn.sendall(b"ERR Another operation running with different terms\n")
                            continue
                    if other_running:
                        conn.sendall(b"ERR Another installation is active or requires administrator reconciliation\n")
                        continue

                    op_id = f"op_{verb}_{uuid.uuid4().hex}"
                    op_state = {
                        'verb': verb,
                        'sha': accepted_sha,
                        'status': 'RUNNING',
                        'error': '',
                        'thread': None,
                        'cancelled': False,
                    }
                    active_operations[op_id] = op_state
                    save_operations()

                    def run_op(current_op_id, v, s):
                        try:
                            install(v, s)
                            with operations_lock:
                                if current_op_id in active_operations:
                                    active_operations[current_op_id]['status'] = 'SUCCESS'
                                    save_operations()
                        except Exception as ex:
                            with operations_lock:
                                if current_op_id in active_operations:
                                    active_operations[current_op_id]['status'] = 'FAILED'
                                    active_operations[current_op_id]['error'] = str(ex)
                                    save_operations()

                    op_thread = threading.Thread(target=run_op, args=(op_id, verb, accepted_sha), daemon=False)
                    op_state['thread'] = op_thread
                    op_thread.start()

                    conn.sendall(f"OK {op_id} STARTED\n".encode('utf-8'))

            elif cmd == 'QUERY_INSTALL' and len(parts) == 2:
                q_op_id = parts[1]
                with operations_lock:
                    if q_op_id in active_operations:
                        info = active_operations[q_op_id]
                        if info['status'] == 'RUNNING':
                            conn.sendall(b"STATUS RUNNING\n")
                        elif info['status'] == 'SUCCESS':
                            conn.sendall(b"STATUS SUCCESS\n")
                        elif info['status'] in ('FAILED', 'UNKNOWN'):
                            err_str = info['error'].replace('\n', ' ').strip()
                            conn.sendall(f"STATUS {info['status']} {err_str}\n".encode('utf-8'))
                    else:
                        conn.sendall(b"ERR Unknown operation; reconcile outcome before retry\n")

            elif cmd == 'CANCEL_INSTALL' and len(parts) == 2:
                c_op_id = parts[1]
                with operations_lock:
                    if c_op_id in active_operations:
                        info = active_operations[c_op_id]
                        if info['status'] == 'RUNNING':
                            # Do not unlock/retry while an MSI is still running.
                            info['cancelled'] = True
                            conn.sendall(b"OK DEFERRED Installer running; waiting for safe completion\n")
                        else:
                            conn.sendall(b"ERR Operation already completed\n")
                    else:
                        conn.sendall(b"ERR No such operation\n")

            elif cmd == 'REVIEW_INFO' and len(parts) == 2:
                try:
                    review_meta(parts[1])
                    state, total = review_state(parts[1])
                    conn.sendall(f"OK {total} {len(review_packages(parts[1]))} {1 if state == 'cached' else 0}\n".encode('utf-8'))
                except Exception as err:
                    conn.sendall(f"ERR {err}\n".encode('utf-8'))

            elif cmd == 'START_REVIEW' and len(parts) == 2:
                if not active_oobe_stage(stage_file):
                    conn.sendall(b"ERR Setup OOBE stage expired; review download rejected\n")
                    continue
                verb = parts[1]
                try:
                    review_meta(verb)
                except Exception as err:
                    conn.sendall(f"ERR {err}\n".encode('utf-8'))
                    continue
                with operations_lock:
                    running_review = [(key, value) for key, value in review_operations.items()
                                      if value['status'] == 'RUNNING']
                    if running_review:
                        key, value = running_review[0]
                        if value['verb'] == verb:
                            conn.sendall(f"OK {key} STARTED\n".encode('utf-8'))
                        else:
                            conn.sendall(b"ERR Another review download is active\n")
                        continue
                    review_id = f"review_{verb}_{uuid.uuid4().hex}"
                    record = {'verb': verb, 'status': 'RUNNING', 'done': 0,
                              'total': review_total_bytes(verb), 'error': '',
                              'fingerprint': '', 'cancel': threading.Event(), 'thread': None}
                    review_operations[review_id] = record
                    # Bound memory: retain only the newest finished review records.
                    # Their terms remain on disk and reopen via GET_TERMS cached-only.
                    finished = [key for key, value in review_operations.items()
                                if value['status'] != 'RUNNING']
                    for key in finished[:-REVIEW_OPERATION_RETENTION]:
                        del review_operations[key]

                    def run_review(key, v, state):
                        def progress(done, total):
                            with operations_lock:
                                state['done'], state['total'] = done, total
                        try:
                            _, fingerprint = review_download(
                                v, progress=progress, cancel=state['cancel'],
                                trace=lambda method, url, size: print('REVIEW', method, url, size, file=sys.stderr, flush=True))
                            outcome = ('SUCCESS', fingerprint, '')
                        except ReviewCancelled:
                            outcome = ('CANCELLED', '', '')
                        except Exception as ex:
                            outcome = ('FAILED', '', str(ex))
                        with operations_lock:
                            state['status'], state['fingerprint'], state['error'] = outcome

                    # Review threads never touch install state; a stuck read must not
                    # hold process exit, and .part cleanup is idempotent on restart.
                    record['thread'] = threading.Thread(target=run_review, args=(review_id, verb, record), daemon=True)
                    record['thread'].start()
                    conn.sendall(f"OK {review_id} STARTED\n".encode('utf-8'))

            elif cmd == 'QUERY_REVIEW' and len(parts) == 2:
                with operations_lock:
                    state = review_operations.get(parts[1])
                    if state is None:
                        conn.sendall(b"ERR Unknown review operation\n")
                    elif state['status'] == 'RUNNING':
                        conn.sendall(f"STATUS RUNNING {state['done']} {state['total']}\n".encode('utf-8'))
                    elif state['status'] == 'SUCCESS':
                        conn.sendall(f"STATUS SUCCESS {state['fingerprint']}\n".encode('utf-8'))
                    elif state['status'] == 'CANCELLED':
                        conn.sendall(b"STATUS CANCELLED\n")
                    else:
                        conn.sendall(f"STATUS FAILED {state['error'].replace(chr(10), ' ').strip()}\n".encode('utf-8'))

            elif cmd == 'CANCEL_REVIEW' and len(parts) == 2:
                with operations_lock:
                    state = review_operations.get(parts[1])
                    if state is not None and state['status'] == 'RUNNING':
                        state['cancel'].set()
                        conn.sendall(b"OK CANCELLING\n")
                    else:
                        conn.sendall(b"ERR No running review operation\n")

            elif cmd == 'GET_RECEIPTS' and len(parts) == 1:
                installed = get_installed_receipts()
                conn.sendall(('OK ' + ' '.join(installed) + '\n').encode('utf-8'))

            else:
                conn.sendall(b"ERR Unknown or malformed command\n")

        except Exception as err:
            print('Coordinator client error:', err, file=sys.stderr)
        finally:
            try:
                conn.shutdown(socket.SHUT_WR)
            except OSError:
                pass
            try:
                conn.close()
            except OSError:
                pass

    try:
        server.close()
    except OSError:
        pass
    sock_path.unlink(missing_ok=True)
    # Revoke new requests before waiting for workers at their safe completion.
    for info in review_operations.values():
        info['cancel'].set()
    deadline = time.monotonic() + REVIEW_SHUTDOWN_JOIN_SECONDS
    for info in review_operations.values():
        thread = info.get('thread')
        if thread is not None and thread.is_alive():
            # Review work is best-effort: partials are removed idempotently on the
            # next run, so a wedged reader must not hold the process open forever.
            thread.join(timeout=max(0.1, deadline - time.monotonic()))
    for info in active_operations.values():
        if info['thread'] and info['thread'].ident is not None:
            # Install threads guard machine state; they must finish before exit.
            info['thread'].join()
    print('OOBE coordinator stopped.', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', nargs=2, metavar=('VERB', 'ACCEPTED_SHA256'), help=argparse.SUPPRESS)
    parser.add_argument('--prepare-terms', metavar='VERB', help=argparse.SUPPRESS)
    parser.add_argument('--review-download', metavar='VERB', help=argparse.SUPPRESS)
    parser.add_argument('--uninstall', metavar='VERB', help='Remove a component that supports it (uwp-xaml), as root')
    parser.add_argument('--refresh-uwp-frameworks', action='store_true',
                        help='Update-time repair, as root: add the class lines to the records of installed UWP frameworks')
    parser.add_argument('--oobe-coordinator', action='store_true', help='Run OOBE setup coordinator daemon')
    parser.add_argument('--apply-user-defaults', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--reconcile-operation', metavar='OP_ID', help='Reconcile an inspected interrupted install while coordinator is stopped')
    parser.add_argument('--reconcile-action', choices=['FAILED'], help='Never guesses success or rollback')
    parser.add_argument('--reconcile-note', help='Required inspection record; do not include credentials')
    parser.add_argument('action', nargs='?', choices=['install', 'list', 'review'])
    parser.add_argument('verbs', nargs='*')
    args = parser.parse_args()
    privileged_actions = sum(bool(value) for value in (args.install, args.prepare_terms, args.review_download,
                             args.oobe_coordinator, args.apply_user_defaults, args.reconcile_operation,
                             args.uninstall, args.refresh_uwp_frameworks))
    if privileged_actions > 1 or (privileged_actions and (args.action or args.verbs)):
        parser.error('Choose exactly one backend action, without unrelated positional arguments')
    if args.reconcile_operation:
        if args.reconcile_action != 'FAILED' or not args.reconcile_note:
            parser.error('Reconciliation requires --reconcile-action FAILED and --reconcile-note')
        print(json.dumps(reconcile_operation(args.reconcile_operation, args.reconcile_note), sort_keys=True))
    elif args.reconcile_action or args.reconcile_note:
        parser.error('Reconciliation options require --reconcile-operation')
    elif args.apply_user_defaults:
        apply_user_defaults()
    elif args.oobe_coordinator:
        oobe_coordinator()
    elif args.prepare_terms:
        prepare_terms(args.prepare_terms)
    elif args.review_download:
        last = [-1]

        def show(done, total):
            percent = done * 100 // max(total, 1)
            if percent != last[0]:
                last[0] = percent
                print(f'Downloading for review: {percent}%', file=sys.stderr, flush=True)
        review_download(args.review_download, progress=show,
                        trace=lambda method, url, size: print('REVIEW', method, url, size, file=sys.stderr, flush=True))
    elif args.install:
        install(*args.install)
    elif args.uninstall:
        uninstall(args.uninstall)
    elif args.refresh_uwp_frameworks:
        if os.geteuid() != 0:
            parser.error('--refresh-uwp-frameworks runs as root')
        refreshed, skipped = refresh_uwp_frameworks_records()
        for full_name in refreshed:
            print('UWP frameworks: class lines added to the record of ' + full_name)
        for full_name, reason in skipped:
            print(f'UWP frameworks: record of {full_name} left alone: {reason}', file=sys.stderr)
        if skipped:
            sys.exit(1)
    elif args.action == 'list':
        for verb, meta in COMPONENTS_METADATA.items():
            status = f" (disabled: {meta['disabled_reason']})" if meta.get('disabled') else ''
            print(f"{verb}: {meta['title']}{status}")
    elif args.action == 'review':
        # Review-only path: explicit, user-facing, and never an installation.
        # Reading cached terms needs no elevation; a download asks first.
        if len(args.verbs) != 1:
            parser.error('review requires exactly one supported component')
        verb = validate_verb(args.verbs[0])
        if not review_available(verb):
            raise ValueError(f'{COMPONENTS[verb]} has no embedded license terms to review.')
        review_only(verb, gui=False)
    else:
        if args.action == 'install' and not args.verbs:
            parser.error('install requires at least one supported component')
        frontend(args.verbs, gui=args.action is None)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'Windows Components: {error}', file=sys.stderr)
        if len(sys.argv) == 1:
            subprocess.run(['zenity', '--error', '--text=' + str(error)])
        sys.exit(1)
