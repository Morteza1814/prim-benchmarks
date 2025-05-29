import os 
import sys
import getpass
import subprocess
import csv

rootdir = "/home/upmem0090/rgq5aw/prim-benchmarks" # Include path to repo

applications = {"GEMV"     : ["NR_DPUS=X NR_TASKLETS=Y BL=Z make all", "./bin/gemv_host -m #elements -n #vec_len"],}

def get_gemv_dims_from_csv(csv_file_path):
    matrix_dims = []
    vec_dims = []
    try:
        # Assuming the CSV format is m,n,k and we want rows where m=1
        awk_script = 'awk -F"," \'$1 == 1 {print $2 "," $3}\''
        awk_command = f"{awk_script} {csv_file_path}"
        #awk_command = f"awk -F',' '$1 == 1 {{print $2 "," $3}}' {csv_file_path}"
        process = subprocess.run(awk_command, shell=True, capture_output=True, text=True)
        if process.returncode != 0:
            print(f"Error running awk command: {process.stderr}")
            return [], []
        
        # Parse the output
        for line in process.stdout.strip().split('\n'):
            if line:
                n_str, k_str = line.split(',')
                try:
                    matrix_dims.append(int(n_str))
                    vec_dims.append(int(k_str))
                except ValueError:
                    print(f"Could not parse line: {line}")

    except Exception as e:
        print(f"An error occurred while processing the CSV: {e}")
        return [], []
        
    return matrix_dims, vec_dims

def run(app_name):
    
    NR_DPUS = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048]
    #NR_DPUS = [2048, 1024, 512, 256]
    #NR_DPUS = [1, 2048]
    #NR_DPUS = [1024, 2048]
    NR_TASKLETS = [1, 2, 4, 8, 16]
    #size = 1
    BL = [10] 
    if(app_name == "GEMV"):
        #size = 1024
        # Read dimensions from CSV
        csv_path = os.path.join(rootdir, "gemv_dims", "dim.csv")
        matrix_dim, vec_dim = get_gemv_dims_from_csv(csv_path)

        if not matrix_dim or not vec_dim:
            print("Could not load GEMV dimensions from CSV. Exiting.")
            return

    if app_name in applications:
        print ("------------------------ Running: "+app_name+"----------------------")
        print ("--------------------------------------------------------------------")
        if(len(applications[app_name]) > 1):
            make = applications[app_name][0]
            run_cmd = applications[app_name][1]
        
            os.chdir(rootdir + "/"+app_name)
            os.getcwd()
        
            os.system("make clean")

            try:
                os.mkdir(rootdir + "/"+ app_name +"/bin")
            except OSError:
                print ("Creation of the direction /bin failed")
                
            try:
                os.mkdir(rootdir + "/"+ app_name +"/log")
            except OSError:
                print ("Creation of the direction /log failed")
            
            try:
                os.mkdir(rootdir + "/"+ app_name +"/log/host")
            except OSError: 
                print ("Creation of the direction /log/host failed")

            try:
                os.mkdir(rootdir + "/"+ app_name +"/profile")
            except OSError:
                print ("Creation of the direction /profile failed")
        

            for r in NR_DPUS:
                for t in NR_TASKLETS:
                    for b in BL:

                        for i in range(len(matrix_dim)): 
                            m = make.replace("X", str(r))
                            m = m.replace("Y", str(t))
                            m = m.replace("Z", str(b))
                            print ("Running = " + m) 
                            try:
                                os.system(m)
                            except: 
                                pass 

                        
                            if(app_name == "GEMV"):
                                r_cmd = run_cmd.replace("#elements", str(matrix_dim[i]))
                                r_cmd = r_cmd.replace("#vec_len", str(vec_dim[i]))
    
                            r_cmd = r_cmd +  " >> profile/out_tl"+str(t)+"_bl"+str(b)+"_dpus"+str(r)+"_M"+str(matrix_dim[i])+"_V"+str(vec_dim[i]) 
                            
                            print ("Running = " + app_name + " -> "+ r_cmd)
                            try:
                                os.system(r_cmd) 
                            except:  
                                pass 
        else:
            make = applications[app_name] 

            os.chdir(rootdir + "/"+app_name)
            os.getcwd()
        
            try:
                os.mkdir(rootdir + "/"+ app_name +"/bin")
                os.mkdir(rootdir + "/"+ app_name +"/log")
                os.mkdir(rootdir + "/"+ app_name +"/log/host")
                os.mkdir(rootdir + "/"+ app_name +"/profile")
            except OSError:
                print ("Creation of the direction failed")

            print (make)    
            os.system(make + ">& profile/out")

    else:
        print ( "Application "+app_name+" not available" )

def main():
    if(len(sys.argv) < 2):
        print ("Usage: python run.py application")
        print ("Applications available: ")
        for key, value in applications.items():
            print (key )
        print ("All")

    else:
        cmd = sys.argv[1]
        print ("Application to run is: " + cmd )
        if cmd == "All":
            for key, value in applications.items():
                run(key)
                os.chdir(rootdir)
        else:
            run(cmd)

if __name__ == "__main__":
    main()

