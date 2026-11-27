"""Compare an unchanged controller fixture on the PR and its exact base.

This records an existing failing general-CI test; it does not mark that test
successful, disable it, or change the general CI workflow.
"""
import hashlib,json,os,subprocess,sys
from pathlib import Path

candidate,baseline=map(lambda x:Path(x).resolve(),sys.argv[1:3])
rows=[]
for root in [baseline,candidate]:
    build=root/'controller-baseline-build'
    commands=[['cmake','-S',str(root),'-B',str(build),'-DCMAKE_BUILD_TYPE=Debug','-DBUILD_ABI_REGRESSION_TESTS=ON'],
              ['cmake','--build',str(build),'--parallel','2']]
    for command in commands:subprocess.run(command,check=True)
    compile_command=[os.environ.get('CC','gcc'),'-ffunction-sections','-fdata-sections','-DHAS_SOCKLEN_T',
        '-I'+str(root/'src'),'-I'+str(root/'enet/include'),'-I'+str(root/'nanors'),'-I'+str(root/'nanors/deps'),'-I'+str(root/'nanors/deps/obl'),
        str(root/'tests/test_input_motion.c'),'-L'+str(build),'-lmoonlight-common-c','-Wl,--gc-sections','-Wl,-rpath,'+str(build),'-pthread','-o',str(build/'test-input_motion')]
    subprocess.run(compile_command,check=True)
    result=subprocess.run([str(build/'test-input_motion')],capture_output=True,text=True)
    hashes={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ['src/InputStream.c','tests/test_input_motion.c']}
    output=result.stdout+result.stderr
    # File paths may differ; the fixture's assertions and negative inputs must not.
    normalized=output.replace(str(root),'<source>')
    rows.append(dict(commit=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),exit_code=result.returncode,hashes=hashes,output=normalized,compile_command=compile_command))
    print(json.dumps({k:v for k,v in rows[-1].items() if k!='compile_command'},indent=2))
Path('controller-baseline-evidence.json').write_text(json.dumps(rows,indent=2))
assert rows[0]['hashes']==rows[1]['hashes'],'Controller sources/fixture differ; investigate independently'
assert rows[0]['exit_code']==rows[1]['exit_code']!=0,'Existing failure did not reproduce identically'
assert rows[0]['output']==rows[1]['output'],'Controller failure changed on the candidate'
print('Identical existing controller failure on exact base and candidate; general CI remains a separate failing check.')
