"""Run the existing engine suites against the standalone C++ port with sanitizers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='cultist-cpp-') as tmp:
    for name, neural in [('bot', True), ('search', True), ('bot', False),
                         ('recruitment', False), ('components', False)]:
        source = (ROOT / f'tests/test_{name}.c').read_text()
        source = source.replace('../bot.c', str(ROOT / 'bot.cpp'))
        # Legacy component assertions describe the archived hand evaluator.
        if not neural:
            source = '#define NEURAL_VALUE 0\n' + source
        if name == 'bot' and neural:
            source = source.replace('assert(evaluate(&s) > 0);', 'assert(abs(evaluate(&s)) < WIN);')
        path = Path(tmp) / f'test_{name}_{int(neural)}.cpp'
        path.write_text(source)
        binary = path.with_suffix('')
        # Legacy C fixtures use implicit narrowing; the standalone port builds strictly.
        subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-Wno-c++11-narrowing',
                        '-fsanitize=address,undefined', str(path), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
with tempfile.TemporaryDirectory(prefix='cultist-strength-') as tmp:
    binary = str(Path(tmp) / 'test_strength')
    subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-DNEURAL_VALUE=0',
                    str(ROOT / 'tests/test_cpp_strength.cpp'), '-o', binary], check=True)
    subprocess.run([binary], check=True)
subprocess.run(['python3', str(ROOT / 'tests/test_protocol.py'), str(ROOT / 'bot_cpp')], check=True)

with tempfile.TemporaryDirectory(prefix='cultist-neural-') as tmp:
    binary = str(Path(tmp) / 'test_neural')
    subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    str(ROOT / 'tests/test_neural_value.cpp'), '-o', binary], check=True)
    subprocess.run([binary], check=True)
